#include "sd_storage.h"

#include "config.h"

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace mrkenddo {
namespace {
constexpr char kMount[] = "/sdcard";
constexpr char kTemporarySuffix[] = ".robot-upload";
constexpr uint32_t kMountCommand = 1;
constexpr uint32_t kUnmountCommand = 2;
constexpr uint32_t kRefreshCommand = 3;
constexpr uint32_t kStopCommand = 4;
constexpr unsigned kMaxDeleteDepth = 16;
constexpr size_t kMaxPhotoIndexEntries = 256;
constexpr char kTag[] = "RobotSD";
using Code = SdStorage::Code;
using Result = SdStorage::Result;

class SdAudioInput final : public NotifyAudioInput {
public:
    SdAudioInput(SdStorage& storage, std::string path)
        : storage_(storage), path_(std::move(path)) {}
    ~SdAudioInput() override { Close(); }

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

void AppendUtf8(std::string& output, uint32_t value) {
    if (value < 0x80) {
        output.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value < 0x10000 && (value < 0xd800 || value > 0xdfff)) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

std::string DecodeId3Text(const uint8_t* data, size_t size) {
    if (!data || size < 2)
        return {};
    const uint8_t encoding = data[0];
    std::string result;
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 1; i < size && data[i] != 0; ++i) {
            if (result.size() >= 128)
                break;
            if (encoding == 3) {
                result.push_back(static_cast<char>(data[i]));
            } else {
                AppendUtf8(result, data[i]);
            }
        }
        if (ValidUtf8(result))
            return result;
        return {};
    }
    if (encoding != 1 && encoding != 2)
        return {};
    size_t offset = 1;
    bool little_endian = encoding == 1;
    if (encoding == 1 && size >= 3 && data[1] == 0xff && data[2] == 0xfe) {
        offset = 3;
        little_endian = true;
    } else if (encoding == 1 && size >= 3 && data[1] == 0xfe && data[2] == 0xff) {
        offset = 3;
        little_endian = false;
    }
    while (offset + 1 < size) {
        uint16_t code = little_endian
                            ? static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8))
                            : static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
        offset += 2;
        if (code == 0)
            break;
        if (code >= 0xd800 && code <= 0xdbff && offset + 1 < size) {
            uint16_t low = little_endian
                               ? static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8))
                               : static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                offset += 2;
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        AppendUtf8(result, code);
        if (result.size() >= 128)
            break;
    }
    return result;
}

uint32_t ReadSynchsafe(const uint8_t* value) {
    return (static_cast<uint32_t>(value[0] & 0x7f) << 21) |
           (static_cast<uint32_t>(value[1] & 0x7f) << 14) |
           (static_cast<uint32_t>(value[2] & 0x7f) << 7) | (value[3] & 0x7f);
}

std::string FoldText(const std::string& value) {
    std::string folded;
    folded.reserve(value.size());
    for (size_t i = 0; i < value.size();) {
        const auto first = static_cast<uint8_t>(value[i]);
        uint32_t code = first;
        size_t length = 1;
        if ((first & 0xe0) == 0xc0 && i + 1 < value.size()) {
            code = ((first & 0x1f) << 6) | (static_cast<uint8_t>(value[i + 1]) & 0x3f);
            length = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < value.size()) {
            code = ((first & 0x0f) << 12) | ((static_cast<uint8_t>(value[i + 1]) & 0x3f) << 6) |
                   (static_cast<uint8_t>(value[i + 2]) & 0x3f);
            length = 3;
        } else if ((first & 0xf8) == 0xf0 && i + 3 < value.size()) {
            code = ((first & 7) << 18) | ((static_cast<uint8_t>(value[i + 1]) & 0x3f) << 12) |
                   ((static_cast<uint8_t>(value[i + 2]) & 0x3f) << 6) |
                   (static_cast<uint8_t>(value[i + 3]) & 0x3f);
            length = 4;
        }
        if ((code >= 0x00c0 && code <= 0x00d6) || (code >= 0x00d8 && code <= 0x00de))
            code += 0x20;
        else if ((code >= 0x0100 && code <= 0x012f && code % 2 == 0) ||
                 (code >= 0x0132 && code <= 0x0137 && code % 2 == 0) ||
                 (code >= 0x0139 && code <= 0x0148 && code % 2 == 1) ||
                 (code >= 0x014a && code <= 0x0177 && code % 2 == 0) ||
                 (code >= 0x0179 && code <= 0x017e && code % 2 == 1) ||
                 (code >= 0x1ea0 && code <= 0x1ef8 && code % 2 == 0))
            ++code;
        else if (code == 0x0178)
            code = 0x00ff;
        AppendUtf8(folded, code);
        i += length;
    }
    return folded;
}

bool HasAudioExtension(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    auto extension = FoldText(path.substr(dot));
    return extension == ".mp3" || extension == ".ogg" || extension == ".opus";
}

void ReadId3Metadata(const std::string& full_path, SdStorage::MusicTrack& track, uint8_t* tag,
                     size_t capacity) {
    track.title = track.path.substr(track.path.find_last_of('/') + 1);
    auto dot = track.title.find_last_of('.');
    if (dot != std::string::npos)
        track.title.resize(dot);
    if (FoldText(track.path.substr(track.path.find_last_of('.'))) != ".mp3" || !tag ||
        capacity == 0)
        return;

    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full_path.c_str(), "rb"), fclose);
    if (!file)
        return;
    std::array<uint8_t, 10> header = {};
    if (fread(header.data(), 1, header.size(), file.get()) != header.size() ||
        std::memcmp(header.data(), "ID3", 3) != 0 || header[3] < 3 || header[3] > 4)
        return;
    const uint32_t tag_size = ReadSynchsafe(header.data() + 6);
    const size_t read_size = std::min<size_t>(tag_size, capacity);
    const size_t actual = fread(tag, 1, read_size, file.get());
    size_t offset = 0;
    while (offset + 10 <= actual) {
        const uint8_t* frame = tag + offset;
        if (frame[0] == 0)
            break;
        uint32_t frame_size = header[3] == 4
                                  ? ReadSynchsafe(frame + 4)
                                  : (static_cast<uint32_t>(frame[4]) << 24) |
                                        (static_cast<uint32_t>(frame[5]) << 16) |
                                        (static_cast<uint32_t>(frame[6]) << 8) | frame[7];
        if (frame_size == 0 || frame_size > actual - offset - 10)
            break;
        std::string* destination = nullptr;
        if (std::memcmp(frame, "TIT2", 4) == 0)
            destination = &track.title;
        else if (std::memcmp(frame, "TPE1", 4) == 0)
            destination = &track.artist;
        else if (std::memcmp(frame, "TALB", 4) == 0)
            destination = &track.album;
        else if (std::memcmp(frame, "TCON", 4) == 0)
            destination = &track.genre;
        if (destination) {
            auto value = DecodeId3Text(frame + 10, frame_size);
            if (!value.empty())
                *destination = std::move(value);
        }
        offset += 10 + frame_size;
    }
}
}  // namespace

SdStorage::~SdStorage() {
    if (worker_ != nullptr) {
        xTaskNotify(worker_, kStopCommand, eSetValueWithOverwrite);
        xSemaphoreTake(stopped_, portMAX_DELAY);
    }
    if (stopped_)
        vSemaphoreDelete(stopped_);
    InvalidatePhotoIndex();
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
    InvalidatePhotoIndex();
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
    InvalidatePhotoIndex();
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

SdStorage::Result SdStorage::SearchMusic(const std::string& query, const std::string& artist,
                                         const std::string& album, const std::string& genre,
                                         size_t limit, std::vector<MusicTrack>& tracks,
                                         bool& truncated) {
    if (limit == 0 || limit > 10 || query.size() > 96 || artist.size() > 96 || album.size() > 96 ||
        genre.size() > 96)
        return {Code::Invalid, "Gi?i h?n t�m ki?m nh?c kh�ng h?p l? (limit 1-10)."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Th? dang b?n."};
    if (auto result = Ready(); !result)
        return result;

    if (!ValidUtf8(query) || !ValidUtf8(artist) || !ValidUtf8(album) || !ValidUtf8(genre))
        return {Code::Invalid, "Search filters must be valid UTF-8."};
    tracks.clear();
    truncated = false;
    const std::string query_folded = FoldText(query);
    const std::string artist_folded = FoldText(artist);
    const std::string album_folded = FoldText(album);
    const std::string genre_folded = FoldText(genre);
    constexpr size_t kMaxTagBytes = 16 * 1024;
    std::unique_ptr<uint8_t[]> tag(new (std::nothrow) uint8_t[kMaxTagBytes]);
    if (!tag)
        return {Code::Unavailable, "Kh?ng d? b? nh? d? d?c ID3."};
    size_t visited = 0;
    size_t audio_files = 0;
    using Directory = std::unique_ptr<DIR, decltype(&closedir)>;
    struct ScanDirectory {
        std::string path;
        Directory handle;
    };
    // Keep the bounded traversal state on the heap. Recursive scanning grows
    // the caller's task stack once per nested directory, including ID3/FatFs.
    std::vector<ScanDirectory> directories;
    directories.reserve(9);  // Root plus eight directory levels.
    DIR* root = opendir(kMount);
    if (!root)
        return IoError("opendir");
    directories.push_back({"/", Directory(root, closedir)});
    while (!directories.empty() && !truncated) {
        auto& directory = directories.back();
        auto* item = readdir(directory.handle.get());
        if (item) {
            if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0 ||
                item->d_name[0] == '.')
                continue;
            if (++visited > 1500) {
                truncated = true;
                break;
            }
            if (visited % 32 == 0)
                vTaskDelay(1);
            std::string child_path =
                (directory.path == "/" ? "" : directory.path) + "/" + item->d_name;
            std::string child_full;
            if (!Resolve(child_path, child_full, false))
                continue;
            struct stat info = {};
            if (stat(child_full.c_str(), &info) != 0)
                continue;
            if (S_ISDIR(info.st_mode)) {
                if (directories.size() < 9) {
                    if (DIR* child = opendir(child_full.c_str()))
                        directories.push_back({std::move(child_path), Directory(child, closedir)});
                }
                continue;
            }
            if (!S_ISREG(info.st_mode) || !HasAudioExtension(child_path))
                continue;
            if (++audio_files > 300) {
                truncated = true;
                break;
            }
            MusicTrack track;
            track.path = child_path;
            track.size = static_cast<uint64_t>(info.st_size);
            ReadId3Metadata(child_full, track, tag.get(), kMaxTagBytes);
            const std::string title = FoldText(track.title);
            const std::string track_artist = FoldText(track.artist);
            const std::string track_album = FoldText(track.album);
            const std::string track_genre = FoldText(track.genre);
            const std::string path_folded = FoldText(track.path);
            if ((!query_folded.empty() && title.find(query_folded) == std::string::npos &&
                 track_artist.find(query_folded) == std::string::npos &&
                 track_album.find(query_folded) == std::string::npos &&
                 path_folded.find(query_folded) == std::string::npos) ||
                (!artist_folded.empty() && track_artist.find(artist_folded) == std::string::npos &&
                 path_folded.find(artist_folded) == std::string::npos) ||
                (!album_folded.empty() && track_album.find(album_folded) == std::string::npos) ||
                (!genre_folded.empty() && track_genre.find(genre_folded) == std::string::npos &&
                 path_folded.find(genre_folded) == std::string::npos))
                continue;
            if (tracks.size() == limit) {
                truncated = true;
                break;
            }
            tracks.push_back(std::move(track));
        } else {
            directories.pop_back();
        }
    }
    return {};
}

void SdStorage::InvalidatePhotoIndex() {
    if (photo_index_ != nullptr)
        heap_caps_free(photo_index_);
    photo_index_ = nullptr;
    photo_count_ = 0;
    photo_index_valid_ = false;
    photo_index_truncated_ = false;
}

SdStorage::Result SdStorage::BuildPhotoIndex() {
    if (photo_index_valid_)
        return {};
    if (photo_index_ != nullptr) {
        heap_caps_free(photo_index_);
        photo_index_ = nullptr;
    }
    photo_count_ = 0;
    photo_index_truncated_ = false;
    if (auto result = Ready(); !result)
        return result;

    constexpr char kPhotoRoot[] = "/robot/images";
    constexpr size_t kMaxVisitedEntries = 2000;
    constexpr char kPhotoRootPrefix[] = "/robot/images/";
    using Directory = std::unique_ptr<DIR, decltype(&closedir)>;
    struct ScanDirectory {
        std::string path;
        Directory handle;
    };
    std::vector<ScanDirectory> directories;
    directories.reserve(9);
    DIR* root = opendir((std::string(kMount) + kPhotoRoot).c_str());
    if (root == nullptr) {
        if (errno != ENOENT)
            return IoError("open photo root");
        photo_index_valid_ = true;
        return {};
    }
    photo_index_ = static_cast<PhotoIndexEntry*>(heap_caps_calloc(
        kMaxPhotoIndexEntries, sizeof(PhotoIndexEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (photo_index_ == nullptr) {
        closedir(root);
        return {Code::Unavailable, "Không đủ PSRAM để lập chỉ mục ảnh."};
    }
    directories.push_back({kPhotoRoot, Directory(root, closedir)});
    size_t visited = 0;
    while (!directories.empty()) {
        auto& directory = directories.back();
        errno = 0;
        dirent* item = readdir(directory.handle.get());
        if (item == nullptr) {
            if (errno != 0)
                return IoError("read photo directory");
            directories.pop_back();
            continue;
        }
        if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0 ||
            item->d_name[0] == '.')
            continue;
        if (++visited > kMaxVisitedEntries) {
            photo_index_truncated_ = true;
            break;
        }
        if (visited % 32 == 0)
            vTaskDelay(1);
        const std::string path = directory.path + "/" + item->d_name;
        std::string full;
        if (!Resolve(path, full, false))
            continue;
        struct stat info = {};
        if (stat(full.c_str(), &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode)) {
            if (directories.size() < 9) {
                if (DIR* child = opendir(full.c_str()))
                    directories.push_back({path, Directory(child, closedir)});
            } else {
                photo_index_truncated_ = true;
            }
            continue;
        }
        const auto extension_pos = path.find_last_of('.');
        if (!S_ISREG(info.st_mode) || extension_pos == std::string::npos)
            continue;
        const auto extension = FoldText(path.substr(extension_pos));
        if (extension != ".jpg" && extension != ".jpeg")
            continue;
        if (photo_count_ >= kMaxPhotoIndexEntries) {
            photo_index_truncated_ = true;
            break;
        }
        if (path.size() >= sizeof(photo_index_[photo_count_].path) ||
            path.rfind(kPhotoRootPrefix, 0) != 0) {
            photo_index_truncated_ = true;
            continue;
        }
        std::memcpy(photo_index_[photo_count_].path, path.c_str(), path.size() + 1);
        photo_index_[photo_count_].size = static_cast<uint64_t>(info.st_size);
        ++photo_count_;
    }
    photo_index_valid_ = true;
    return {};
}

SdStorage::Result SdStorage::SearchPhotos(const std::string& query, const std::string& album,
                                          size_t limit, std::vector<Photo>& photos,
                                          bool& truncated) {
    if (query.size() > 96 || album.size() > 96 || limit == 0 || limit > 10 || !ValidUtf8(query) ||
        !ValidUtf8(album))
        return {Code::Invalid, "Bộ lọc ảnh không hợp lệ (giới hạn 1-10 kết quả)."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;

    photos.clear();
    truncated = photo_index_truncated_;
    const auto query_folded = FoldText(query);
    const auto album_folded = FoldText(album);
    for (size_t i = 0; i < photo_count_; ++i) {
        const auto& entry = photo_index_[i];
        const std::string path(entry.path);
        const auto slash = path.find_last_of('/');
        const auto extension_pos = path.find_last_of('.');
        Photo photo;
        photo.path = path;
        photo.size = entry.size;
        photo.name = path.substr(slash + 1, extension_pos - slash - 1);
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        if (slash > kPhotoRootLength)
            photo.album = path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1);
        const auto path_folded = FoldText(path);
        const auto photo_album = FoldText(photo.album);
        if ((!query_folded.empty() && path_folded.find(query_folded) == std::string::npos) ||
            (!album_folded.empty() && photo_album.find(album_folded) == std::string::npos))
            continue;
        if (photos.size() == limit) {
            truncated = true;
            break;
        }
        photos.push_back(photo);
    }
    return {};
}

SdStorage::Result SdStorage::RandomPhoto(const std::string& album, Photo& photo) {
    if (album.size() > 96 || !ValidUtf8(album))
        return {Code::Invalid, "Tên album không hợp lệ."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;

    const auto album_folded = FoldText(album);
    size_t matches = 0;
    for (size_t i = 0; i < photo_count_; ++i) {
        const std::string path(photo_index_[i].path);
        const auto slash = path.find_last_of('/');
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        const std::string item_album =
            slash > kPhotoRootLength
                ? path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1)
                : "";
        if (album_folded.empty() || FoldText(item_album).find(album_folded) != std::string::npos)
            ++matches;
    }
    if (matches == 0)
        return {Code::NotFound, "Không tìm thấy ảnh trong album."};
    size_t selected = esp_random() % matches;
    for (size_t i = 0; i < photo_count_; ++i) {
        const std::string path(photo_index_[i].path);
        const auto slash = path.find_last_of('/');
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        const std::string item_album =
            slash > kPhotoRootLength
                ? path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1)
                : "";
        if (!album_folded.empty() && FoldText(item_album).find(album_folded) == std::string::npos)
            continue;
        if (selected-- == 0) {
            const auto extension_pos = path.find_last_of('.');
            photo.path = path;
            photo.size = photo_index_[i].size;
            photo.name = path.substr(slash + 1, extension_pos - slash - 1);
            photo.album = item_album;
            return {};
        }
    }
    return {Code::NotFound, "Không chọn được ảnh ngẫu nhiên."};
}

SdStorage::Result SdStorage::ReadPhotoJpeg(const std::string& path, uint8_t*& data, size_t& size,
                                           size_t& width, size_t& height) {
    constexpr char kPhotoRoot[] = "/robot/images/";
    if (path.rfind(kPhotoRoot, 0) != 0)
        return {Code::Invalid, "Ảnh MCP chỉ được đọc trong /robot/images/."};
    return ReadJpeg(path, data, size, width, height);
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
        InvalidatePhotoIndex();
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
        InvalidatePhotoIndex();
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
        InvalidatePhotoIndex();
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

SdStorage::Result SdStorage::ReadJpeg(const std::string& path, uint8_t*& data, size_t& size,
                                      size_t& width, size_t& height) {
    data = nullptr;
    size = width = height = 0;
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;

    const size_t basename = path.find_last_of('/') + 1;
    const size_t extension_pos = path.find_last_of('.');
    std::string extension = extension_pos == std::string::npos || extension_pos < basename
                                ? ""
                                : path.substr(extension_pos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".jpg" && extension != ".jpeg")
        return {Code::Invalid, "Chỉ hỗ trợ ảnh JPEG (.jpg, .jpeg)."};

    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;

    constexpr size_t kMaxJpegBytes = 512 * 1024;
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return errno == ENOENT ? Result{Code::NotFound, "Không tìm thấy ảnh."}
                               : IoError("stat JPEG");
    if (!S_ISREG(info.st_mode) || info.st_size < 4 ||
        static_cast<uint64_t>(info.st_size) > kMaxJpegBytes)
        return {Code::Limit, "Ảnh JPEG phải có dung lượng tối đa 512 KiB."};

    const auto length = static_cast<size_t>(info.st_size);
    auto* buffer =
        static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer)
        return {Code::Unavailable, "Không đủ PSRAM để nạp ảnh JPEG."};
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> owner(buffer, heap_caps_free);
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full.c_str(), "rb"), fclose);
    if (!file)
        return IoError("open JPEG");
    if (fread(buffer, 1, length, file.get()) != length || ferror(file.get()))
        return IoError("read JPEG");

    // Validate dimensions before decoding so malformed images cannot request
    // an unbounded RGB565 frame buffer from the JPEG decoder.
    if (buffer[0] != 0xff || buffer[1] != 0xd8)
        return {Code::Invalid, "File không phải ảnh JPEG hợp lệ."};
    size_t position = 2;
    bool found_dimensions = false;
    while (position + 4 <= length) {
        while (position < length && buffer[position] != 0xff)
            ++position;
        while (position < length && buffer[position] == 0xff)
            ++position;
        if (position >= length)
            break;
        const uint8_t marker = buffer[position++];
        if (marker == 0xd9 || marker == 0xda)
            break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (position + 2 > length)
            break;
        const size_t segment_length = (static_cast<size_t>(buffer[position]) << 8) |
                                      static_cast<size_t>(buffer[position + 1]);
        if (segment_length < 2 || segment_length > length - position)
            break;
        const bool is_sof =
            (marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf);
        if (is_sof) {
            if (segment_length < 7)
                break;
            height = (static_cast<size_t>(buffer[position + 3]) << 8) | buffer[position + 4];
            width = (static_cast<size_t>(buffer[position + 5]) << 8) | buffer[position + 6];
            found_dimensions = width != 0 && height != 0 && width <= 512 && height <= 512;
            break;
        }
        position += segment_length;
    }
    if (!found_dimensions)
        return {Code::Limit, "JPEG không hợp lệ hoặc vượt giới hạn 512 × 512 pixel."};

    size = length;
    data = owner.release();
    return {};
}

SdStorage::Result SdStorage::CreateAudioInput(const std::string& path,
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
    if (extension != ".mp3" && extension != ".ogg" && extension != ".opus")
        return {Code::Invalid, "Hỗ trợ phát MP3 và Ogg/Opus (.mp3, .ogg, .opus)."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
        return {Code::Invalid, "File âm thanh không tồn tại, rỗng hoặc không hợp lệ."};

    factory = [this, path]() { return std::make_unique<SdAudioInput>(*this, path); };
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
        return {Code::NotFound, "Không tìm thấy file âm thanh hợp lệ."};
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
