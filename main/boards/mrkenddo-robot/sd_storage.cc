#include "sd_storage.h"

#include "config.h"

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mrkenddo {
namespace {
constexpr char kMount[] = "/sdcard";
constexpr char kTemporarySuffix[] = ".robot-upload";
constexpr uint32_t kMountCommand = 1;
constexpr uint32_t kUnmountCommand = 2;
constexpr uint32_t kRefreshCommand = 3;
constexpr uint32_t kStopCommand = 4;
constexpr unsigned kMaxDeleteDepth = 16;
constexpr char kTag[] = "RobotSD";
using Code = SdStorage::Code;
using Result = SdStorage::Result;

class SdOggInput final : public NotifyAudioInput {
public:
    SdOggInput(SdStorage& storage, std::string path) : storage_(storage), path_(std::move(path)) {}
    ~SdOggInput() override { Close(); }

    bool Open() override { return static_cast<bool>(storage_.OpenAudioFile(path_, file_)); }
    bool Read(char* buffer, size_t capacity, size_t& bytes_read) override {
        return storage_.ReadAudioFile(file_, buffer, capacity, bytes_read);
    }
    void Close() override {
        if (file_ != nullptr) {
            storage_.CloseAudioFile(file_);
            file_ = nullptr;
        }
    }

private:
    SdStorage& storage_;
    std::string path_;
    FILE* file_ = nullptr;
};

bool ValidUtf8(const std::string& text) {
    uint32_t value = 0, minimum = 0;
    int left = 0;
    for (unsigned char c : text) {
        if (left) {
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 0x3f);
            if (--left == 0 &&
                (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)))
                return false;
        } else if (c < 0x80) {
            continue;
        } else if (c >= 0xc2 && c <= 0xdf) {
            left = 1;
            value = c & 0x1f;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            left = 2;
            value = c & 0x0f;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            left = 3;
            value = c & 7;
            minimum = 0x10000;
        } else
            return false;
    }
    return left == 0;
}
}  // namespace

SdStorage::~SdStorage() {
    if (worker_ != nullptr) {
        xTaskNotify(worker_, kStopCommand, eSetValueWithOverwrite);
        xSemaphoreTake(stopped_, portMAX_DELAY);
    }
    if (stopped_)
        vSemaphoreDelete(stopped_);
}

void SdStorage::Start() {
    if (worker_ != nullptr)
        return;
    stopped_ = xSemaphoreCreateBinary();
    if (!stopped_) {
        SetState("error", "Không đủ bộ nhớ để khởi tạo microSD.");
        return;
    }
    if (xTaskCreate(Worker, "robot_sd", 6144, this, 1, &worker_) != pdPASS) {
        vSemaphoreDelete(stopped_);
        stopped_ = nullptr;
        SetState("error", "Không tạo được task microSD.");
        return;
    }
    Request("mount");
}

void SdStorage::Worker(void* context) {
    auto& self = *static_cast<SdStorage*>(context);
    while (true) {
        uint32_t command = 0;
        xTaskNotifyWait(0, UINT32_MAX, &command, portMAX_DELAY);
        {
            std::lock_guard<std::mutex> lock(self.io_mutex_);
            if (command == kStopCommand) {
                self.Unmount();
                break;
            }
            if (command == kMountCommand)
                self.Mount();
            else if (command == kUnmountCommand)
                self.Unmount();
            else if (command == kRefreshCommand)
                self.RefreshSpace();
        }
    }
    xSemaphoreGive(self.stopped_);
    vTaskDelete(nullptr);
}

SdStorage::Status SdStorage::GetStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

void SdStorage::SetState(const char* state, const std::string& message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = state;
    status_.message = message;
    if (status_.state != "mounted")
        status_.space_valid = false;
}

SdStorage::Result SdStorage::Request(const std::string& action) {
    uint32_t command = action == "mount"     ? kMountCommand
                       : action == "unmount" ? kUnmountCommand
                       : action == "refresh" ? kRefreshCommand
                                             : 0;
    if (!command)
        return {Code::Invalid, "Thao tác không hợp lệ."};
    if (command == kUnmountCommand && playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi mới tháo an toàn."};
    if (!worker_)
        return {Code::Unavailable, "Task microSD chưa sẵn sàng."};
    if (xTaskNotify(worker_, command, eSetValueWithoutOverwrite) != pdPASS)
        return {Code::Busy, "MicroSD đang có yêu cầu chờ xử lý."};
    return {};
}

void SdStorage::Mount() {
    if (card_ && GetStatus().state == "error") {
        Unmount();
        if (card_)
            return;
    }
    if (card_) {
        SetState("mounted", "Thẻ đã được kết nối.");
        RefreshSpace();
        return;
    }
    SetState("mounting", "Đang kết nối thẻ nhớ...");
    if (!bus_initialized_) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = SD_MOSI_PIN;
        bus.miso_io_num = SD_MISO_PIN;
        bus.sclk_io_num = SD_SCK_PIN;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = kChunkBytes;
        auto err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            SetState("error", std::string("Không khởi tạo được SPI2: ") + esp_err_to_name(err));
            ESP_LOGW(kTag, "SPI2 init failed: %s", esp_err_to_name(err));
            return;
        }
        bus_initialized_ = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SD_CS_PIN;
    device.gpio_cd = GPIO_NUM_NC;
    device.gpio_wp = GPIO_NUM_NC;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 4;
    sdmmc_card_t* card = nullptr;
    auto err = esp_vfs_fat_sdspi_mount(kMount, &host, &device, &mount, &card);
    if (err != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        bus_initialized_ = false;
        SetState("error", std::string("Không đọc được thẻ: ") + esp_err_to_name(err) +
                              ". Kiểm tra thẻ FAT32, dây nối và nguồn cấp.");
        ESP_LOGW(kTag, "SD mount failed: %s (CS42/SCK41/MOSI40/MISO39)", esp_err_to_name(err));
        return;
    }
    card_ = card;
    SetState("mounted", "MicroSD sẵn sàng.");
    RefreshSpace();
    ESP_LOGI(kTag, "SD ready: %s, SPI2 10MHz", card->cid.name);
}

void SdStorage::Unmount() {
    if (playback_files_.load() != 0) {
        SetState("mounted", "Dừng phát nhạc từ thẻ trước khi tháo microSD an toàn.");
        return;
    }
    if (!upload_path_.empty()) {
        // Only our temporary file is removed; the destination was never replaced.
        std::string full;
        if (Resolve(upload_path_, full, false))
            std::remove((full + kTemporarySuffix).c_str());
        ClearUpload();
    }
    if (card_) {
        auto err = esp_vfs_fat_sdcard_unmount(kMount, static_cast<sdmmc_card_t*>(card_));
        if (err != ESP_OK) {
            SetState("error", std::string("Không tháo được thẻ: ") + esp_err_to_name(err));
            return;
        }
        card_ = nullptr;
    }
    if (bus_initialized_) {
        spi_bus_free(SPI2_HOST);
        bus_initialized_ = false;
    }
    SetState("unmounted", "Đã ngắt kết nối. Có thể tháo thẻ.");
}

void SdStorage::RefreshSpace() {
    if (!card_)
        return;
    uint64_t total = 0, available = 0;
    auto err = esp_vfs_fat_info(kMount, &total, &available);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.space_valid = err == ESP_OK;
    if (err == ESP_OK) {
        status_.total_bytes = total;
        status_.free_bytes = available;
    }
}

void SdStorage::InvalidateSpace() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.space_valid = false;
}

void SdStorage::ClearUpload() {
    upload_path_.clear();
    upload_total_ = upload_offset_ = 0;
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.upload_path.clear();
    status_.uploaded_bytes = 0;
}

SdStorage::Result SdStorage::Ready() const {
    if (!card_ || GetStatus().state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng. Kiểm tra thẻ rồi bấm Kết nối lại."};
    return {};
}

SdStorage::Result SdStorage::IoError(const char* operation) {
    const int error = errno;
    ESP_LOGW(kTag, "%s failed, errno=%d (%s)", operation, error, strerror(error));
    if (error == ENOSPC)
        return {Code::Limit, "Thẻ đã đầy. Hủy upload rồi xóa file để giải phóng dung lượng."};
    if (error == ENOENT || error == ENOTDIR)
        return {Code::NotFound, "Không tìm thấy đường dẫn hoặc thư mục cha."};
    if (error == EEXIST)
        return {Code::Exists, "Tên này đã tồn tại."};
    if (error == EBUSY || error == EMFILE || error == ENFILE)
        return {Code::Busy, "File đang bận. Hãy hoàn tất thao tác hiện tại trước."};
    // FatFs maps FR_DENIED to EACCES. That includes a non-empty directory
    // passed to rmdir(), a read-only item, and a full directory table; these
    // are operation failures, not evidence that the card has become unusable.
    if (error == EACCES || error == EPERM || error == EROFS)
        return {Code::Invalid,
                "Thẻ từ chối thay đổi. Kiểm tra thư mục có rỗng không, thuộc tính chỉ đọc và dung "
                "lượng."};
    SetState("error", "Lỗi đọc/ghi microSD. Tháo an toàn rồi kết nối lại.");
    return {Code::Io, "Lỗi đọc/ghi microSD."};
}

SdStorage::Result SdStorage::Resolve(const std::string& path, std::string& full, bool allow_root) {
    if (path.empty() || path.size() > 192 || path.front() != '/' || !ValidUtf8(path) ||
        (!allow_root && path == "/"))
        return {Code::Invalid, "Đường dẫn không hợp lệ."};
    if (path != "/") {
        size_t begin = 1;
        while (begin < path.size()) {
            size_t end = path.find('/', begin);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = path.substr(begin, end - begin);
            std::string folded = part;
            for (char& c : folded) {
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            }
            if (part.empty() || part.front() == '.' || part.back() == '.' || part.back() == ' ' ||
                part.size() > 120 || folded.find(kTemporarySuffix) != std::string::npos)
                return {Code::Invalid, "Tên file/thư mục không hợp lệ hoặc được dành riêng."};
            begin = end + 1;
        }
        if (path.back() == '/')
            return {Code::Invalid, "Đường dẫn không được kết thúc bằng /."};
    }
    for (unsigned char c : path) {
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '<' ||
            c == '>' || c == '|' || c == '"')
            return {Code::Invalid, "Đường dẫn chứa ký tự không hợp lệ."};
    }
    full = std::string(kMount) + (path == "/" ? "" : path);
    return {};
}

SdStorage::Result SdStorage::List(const std::string& path, size_t offset, Listing& listing) {
    std::string full;
    if (auto result = Resolve(path, full); !result)
        return result;
    if (offset > 10000)
        return {Code::Limit, "Thư mục vượt giới hạn phân trang."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    DIR* raw = opendir(full.c_str());
    if (!raw)
        return errno == ENOENT ? Result{Code::NotFound, "Không tìm thấy thư mục."}
                               : IoError("opendir");
    std::unique_ptr<DIR, decltype(&closedir)> dir(raw, closedir);
    listing = {};
    size_t index = 0;
    size_t scanned = 0;
    errno = 0;
    while (auto* item = readdir(dir.get())) {
        if (++scanned % 32 == 0)
            vTaskDelay(1);
        std::string child;
        if (!Resolve((path == "/" ? "" : path) + "/" + item->d_name, child, false))
            continue;
        if (index++ < offset) {
            if (index % 32 == 0)
                vTaskDelay(1);
            continue;
        }
        if (listing.entries.size() == kPageSize) {
            listing.more = true;
            break;
        }
        struct stat info = {};
        if (stat(child.c_str(), &info) != 0)
            return IoError("stat");
        listing.entries.push_back(
            {item->d_name, S_ISDIR(info.st_mode), static_cast<uint64_t>(info.st_size)});
        errno = 0;
    }
    if (errno)
        return IoError("readdir");
    listing.next_offset = offset + listing.entries.size();
    return {};
}

SdStorage::Result SdStorage::MakeDirectory(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (mkdir(full.c_str(), 0775) == 0) {
        InvalidateSpace();
        return {};
    }
    if (errno == EEXIST)
        return {Code::Exists, "Tên này đã tồn tại."};
    return IoError("mkdir");
}

SdStorage::Result SdStorage::Remove(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (!upload_path_.empty())
        return {Code::Busy, "Hãy hoàn tất hoặc hủy upload trước."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return {Code::NotFound, "Không tìm thấy file/thư mục."};
    const bool is_directory = S_ISDIR(info.st_mode);
    if (is_directory) {
        if (auto result = RemoveTree(full, 0); !result)
            return result;
    }
    int rc = is_directory ? rmdir(full.c_str()) : unlink(full.c_str());
    if (rc == 0) {
        InvalidateSpace();
        return {};
    }
    if (is_directory && (errno == ENOTEMPTY || errno == EEXIST))
        return {Code::Invalid,
                "Thư mục còn mục chưa xóa được hoặc đã thay đổi trong lúc thao tác."};
    return IoError("remove");
}

SdStorage::Result SdStorage::RemoveTree(const std::string& full_path, unsigned depth) {
    if (depth >= kMaxDeleteDepth)
        return {Code::Limit, "Thư mục lồng quá sâu; dừng để giới hạn xóa an toàn."};

    DIR* raw = opendir(full_path.c_str());
    if (!raw)
        return IoError("opendir recursive delete");
    std::unique_ptr<DIR, decltype(&closedir)> dir(raw, closedir);
    size_t visited = 0;
    while (true) {
        errno = 0;
        dirent* item = readdir(dir.get());
        if (!item) {
            if (errno != 0)
                return IoError("readdir recursive delete");
            break;
        }
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;

        const std::string child = full_path + "/" + item->d_name;
        struct stat info = {};
        if (stat(child.c_str(), &info) != 0)
            return IoError("stat recursive delete");

        if (S_ISDIR(info.st_mode)) {
            // Keep only one FATFS directory open while descending.
            dir.reset();
            if (auto result = RemoveTree(child, depth + 1); !result)
                return result;
            if (rmdir(child.c_str()) != 0)
                return IoError("rmdir recursive delete");
            raw = opendir(full_path.c_str());
            if (!raw)
                return IoError("reopen parent after recursive delete");
            dir.reset(raw);
        } else if (unlink(child.c_str()) != 0) {
            return IoError("unlink recursive delete");
        }

        if (++visited % 16 == 0)
            vTaskDelay(1);
    }
    return {};
}

SdStorage::Result SdStorage::Upload(const std::string& path, uint64_t offset, uint64_t total,
                                    const char* chunk, size_t chunk_size) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    if (total > kMaxFileBytes || chunk_size > kUploadChunkBytes || offset > total ||
        chunk_size > total - offset || (chunk_size == 0 && total != 0) ||
        (chunk_size != 0 && chunk == nullptr))
        return {Code::Limit, "File tối đa 1 GiB, mỗi khối tối đa 1 MiB."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (!upload_path_.empty() && upload_path_ != path)
        return {Code::Busy, "Đang upload file khác. Hãy hủy hoặc hoàn tất trước."};
    struct stat info = {};
    if (stat(full.c_str(), &info) == 0)
        return {Code::Exists, "File đã tồn tại. Đổi tên hoặc xóa file cũ trước."};
    if (errno != ENOENT)
        return IoError("stat upload target");
    const std::string temporary = full + kTemporarySuffix;
    if (offset == 0) {
        upload_path_ = path;
        upload_total_ = total;
        upload_offset_ = 0;
    } else if (upload_path_ != path || upload_total_ != total || upload_offset_ != offset) {
        return {Code::Invalid, "Vị trí upload không khớp. Hãy tải lại từ đầu."};
    }
    if (offset > 0 && (stat(temporary.c_str(), &info) != 0 || info.st_size < 0 ||
                       static_cast<uint64_t>(info.st_size) != offset))
        return {Code::Invalid, "File tạm không khớp offset. Hủy upload rồi tải lại từ đầu."};
    std::unique_ptr<FILE, decltype(&fclose)> file(
        fopen(temporary.c_str(), offset == 0 ? "wb" : "ab"), fclose);
    if (!file)
        return IoError("open upload");
    if ((chunk_size != 0 && fwrite(chunk, 1, chunk_size, file.get()) != chunk_size) ||
        fflush(file.get()) != 0)
        return IoError("write upload");
    // Flush every HTTP chunk, but only force a card sync before committing the
    // completed temporary file. This avoids a slow sync after each small request.
    if (upload_offset_ + chunk_size == total && fsync(fileno(file.get())) != 0)
        return IoError("sync upload");
    FILE* completed = file.release();
    if (fclose(completed) != 0)
        return IoError("close upload");
    upload_offset_ += chunk_size;
    InvalidateSpace();
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        status_.upload_path = path;
        status_.uploaded_bytes = upload_offset_;
    }
    if (upload_offset_ == total) {
        if (rename(temporary.c_str(), full.c_str()) != 0)
            return IoError("commit upload");
        ClearUpload();
    }
    return {};
}

SdStorage::Result SdStorage::AbortUpload(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (!card_)
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (std::remove((full + kTemporarySuffix).c_str()) != 0 && errno != ENOENT)
        return IoError("abort upload");
    if (path == upload_path_)
        ClearUpload();
    InvalidateSpace();
    return {};
}

SdStorage::Result SdStorage::Download(const std::string& path,
                                      const std::function<bool(const char*, size_t)>& sink) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return {Code::NotFound, "Không tìm thấy file."};
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<uint64_t>(info.st_size) > kMaxFileBytes)
        return {Code::Limit, "Chỉ tải xuống file thường tối đa 1 GiB."};
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full.c_str(), "rb"), fclose);
    if (!file)
        return IoError("open download");
    auto buffer = std::make_unique<std::array<char, kChunkBytes>>();
    while (true) {
        size_t count = fread(buffer->data(), 1, buffer->size(), file.get());
        if (count && !sink(buffer->data(), count))
            return {Code::Io, "Kết nối tải xuống đã đóng."};
        if (count < buffer->size()) {
            if (ferror(file.get()))
                return IoError("read download");
            break;
        }
        vTaskDelay(1);
    }
    return {};
}

SdStorage::Result SdStorage::CreateOggInput(const std::string& path,
                                            NotifyPlayer::InputFactory& factory) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;

    const size_t basename = path.find_last_of('/') + 1;
    const size_t extension_pos = path.find_last_of('.');
    std::string extension = extension_pos == std::string::npos || extension_pos < basename
                                ? ""
                                : path.substr(extension_pos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".ogg")
        return {Code::Invalid, "Hiện hỗ trợ phát file Ogg/Opus (.ogg)."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
        return {Code::Invalid, "File Ogg không tồn tại, rỗng hoặc không hợp lệ."};

    factory = [this, path]() { return std::make_unique<SdOggInput>(*this, path); };
    return {};
}

SdStorage::Result SdStorage::OpenAudioFile(const std::string& path, FILE*& file) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (auto result = Ready(); !result)
        return result;
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
        return {Code::NotFound, "Không tìm thấy file Ogg hợp lệ."};
    file = fopen(full.c_str(), "rb");
    if (!file)
        return IoError("open local audio");
    playback_files_.fetch_add(1);
    return {};
}

bool SdStorage::ReadAudioFile(FILE* file, char* buffer, size_t capacity, size_t& bytes_read) {
    if (!file || !buffer || capacity == 0)
        return false;
    std::lock_guard<std::mutex> lock(io_mutex_);
    bytes_read = fread(buffer, 1, capacity, file);
    return bytes_read != 0 || !ferror(file);
}

void SdStorage::CloseAudioFile(FILE* file) {
    if (!file)
        return;
    std::lock_guard<std::mutex> lock(io_mutex_);
    fclose(file);
    playback_files_.fetch_sub(1);
}
}  // namespace mrkenddo
