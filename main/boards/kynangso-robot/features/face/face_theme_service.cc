#include "features/face/face_theme_service.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include "core/robot_settings.h"
#include "features/storage/sd_storage.h"

namespace kynangso {
namespace {
constexpr size_t kMaxUserThemes = 16;
constexpr size_t kMaxBuiltInThemes = 8;
constexpr size_t kMaxThemeCatalog = kMaxUserThemes + kMaxBuiltInThemes;
}  // namespace

FaceThemeService::FaceThemeService(RobotSettings& settings, SdStorage& storage,
                                   std::function<void(const FaceTheme&)> apply)
    : settings_(settings), storage_(storage), apply_(std::move(apply)) {
    catalog_ = {
        {{}, true},
        {{"candy", "Candy Rose", "soft", 0xFF8AC6, 0xFFD166, 0x92E7FF, 0x140D22}, true},
        {{"neon", "Neon Violet", "rounded", 0xB794FF, 0x4DFFD2, 0x55D8FF, 0x080C20}, true},
        {{"pixel", "Pixel Mint", "pixel", 0x66FFB2, 0xFFCF5C, 0x73CFFF, 0x03130F}, true},
        {{"aether", "Aether Hologram", "holo", 0x75F5FF, 0xA889FF, 0x48D7FF, 0x041326}, true},
        {{"orbit", "Quantum Orbit", "orbital", 0xA28BFF, 0xFF54D9, 0x62EFFF, 0x08091D}, true},
        {{"prism", "Prism Reactor", "plasma", 0xFF76D8, 0x62F4FF, 0xFFD166, 0x17091D}, true},
        {{"gif-poc", "GIF PoC (SD)", "gif-poc", 0xFFFFFF, 0xFF70AE, 0x69D9FF, 0x000000}, true}};
}

FaceThemeService::~FaceThemeService() {
    stop_.store(true);
    if (worker_ != nullptr)
        xSemaphoreTake(stopped_, portMAX_DELAY);
    if (stopped_ != nullptr)
        vSemaphoreDelete(stopped_);
}

void FaceThemeService::Start() {
    std::string error;
    FaceTheme saved;
    if (ParseFaceTheme(settings_.LoadFaceTheme(), saved, error))
        active_ = std::move(saved);
    apply_(active_);
    stopped_ = xSemaphoreCreateBinary();
    if (stopped_ == nullptr || xTaskCreate(
                                   [](void* context) {
                                       auto* service = static_cast<FaceThemeService*>(context);
                                       service->Run();
                                       xSemaphoreGive(service->stopped_);
                                       vTaskDelete(nullptr);
                                   },
                                   "face_themes", 6144, this, 2, &worker_) != pdPASS) {
        worker_ = nullptr;
        error_ = "Không đủ bộ nhớ để mở kho theme; vẫn giữ khuôn mặt hiện tại.";
    }
}

bool FaceThemeService::Request(const std::string& action, const std::string& id,
                               const std::string& manifest) {
    if ((action != "select" && action != "refresh" && action != "install") || id.size() > 32 ||
        manifest.size() > 2048)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_ || worker_ == nullptr)
        return false;
    action_ = action;
    id_ = id;
    manifest_ = manifest;
    error_.clear();
    message_ = "Đang xử lý";
    busy_ = true;
    ++operation_;
    return true;
}

cJSON* FaceThemeService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return nullptr;
    cJSON_AddBoolToObject(root, "busy", busy_);
    cJSON_AddBoolToObject(root, "sd_available", sd_available_);
    cJSON_AddBoolToObject(root, "truncated", truncated_);
    cJSON_AddNumberToObject(root, "skipped", skipped_);
    cJSON_AddNumberToObject(root, "operation", operation_);
    cJSON_AddStringToObject(root, "error", error_.c_str());
    cJSON_AddStringToObject(root, "message", message_.c_str());
    cJSON_AddStringToObject(root, "active_id", active_.id.c_str());
    cJSON_AddStringToObject(root, "active_name", active_.name.c_str());
    cJSON* items = cJSON_AddArrayToObject(root, "themes");
    for (const auto& entry : catalog_) {
        cJSON* item = cJSON_Parse(EncodeFaceTheme(entry.theme).c_str());
        if (!item)
            continue;
        cJSON_AddBoolToObject(item, "builtin", entry.builtin);
        cJSON_AddItemToArray(items, item);
    }
    return root;
}

std::string FaceThemeService::Refresh() {
    SdStorage::Listing listing;
    const auto listed = storage_.List("/robot/themes", 0, listing);
    std::vector<Entry> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : catalog_)
            if (entry.builtin)
                next.push_back(entry);
    }
    unsigned skipped = 0;
    bool truncated = listing.more;
    for (const auto& file : listing.entries) {
        if (file.directory || file.name.size() < 6 ||
            file.name.substr(file.name.size() - 5) != ".json")
            continue;
        if (next.size() >= kMaxThemeCatalog) {
            truncated = true;
            break;
        }
        if (file.size == 0 || file.size > 2048) {
            ++skipped;
            continue;
        }
        std::string json;
        const auto read =
            storage_.Download("/robot/themes/" + file.name, [&](const char* data, size_t size) {
                if (json.size() + size > 2048)
                    return false;
                json.append(data, size);
                return true;
            });
        FaceTheme theme;
        std::string error;
        if (read.code == SdStorage::Code::Busy)
            return read.message;
        if (!read || !ParseFaceTheme(json, theme, error) || file.name != theme.id + ".json" ||
            std::any_of(next.begin(), next.end(),
                        [&](const Entry& e) { return e.theme.id == theme.id; })) {
            ++skipped;
            continue;
        }
        next.push_back({std::move(theme), false});
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // A busy SD card must not erase a previously loaded catalog.
    if (listed || listed.code == SdStorage::Code::NotFound ||
        listed.code == SdStorage::Code::Unavailable)
        catalog_ = std::move(next);
    sd_available_ = static_cast<bool>(listed) || listed.code == SdStorage::Code::NotFound;
    skipped_ = skipped;
    truncated_ = truncated;
    message_ = listed ? "Đã tải danh sách theme" : "Theme có sẵn vẫn dùng được. " + listed.message;
    return listed || listed.code == SdStorage::Code::NotFound ? std::string{} : listed.message;
}

std::string FaceThemeService::Install(const std::string& manifest) {
    FaceTheme theme;
    std::string error;
    if (!ParseFaceTheme(manifest, theme, error))
        return error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : catalog_)
            if (entry.theme.id == theme.id)
                return "ID theme đã tồn tại; hãy chọn ID mới.";
        const auto user_theme_count = std::count_if(
            catalog_.begin(), catalog_.end(), [](const Entry& entry) { return !entry.builtin; });
        if (user_theme_count >= kMaxUserThemes)
            return "Kho theme đã đủ 16 gói SD. Hãy xóa bớt gói rồi tải lại danh sách.";
    }
    for (const char* path : {"/robot", "/robot/themes"}) {
        auto result = storage_.MakeDirectory(path);
        if (!result && result.code != SdStorage::Code::Exists)
            return result.message;
    }
    const std::string canonical = EncodeFaceTheme(theme);
    if (canonical.empty())
        return "Không đủ bộ nhớ để tạo theme.";
    auto result = storage_.Upload("/robot/themes/" + theme.id + ".json", 0, canonical.size(),
                                  canonical.data(), canonical.size());
    if (!result)
        return result.message;
    return Refresh();
}

// Validate the block structure before exposing untrusted card data to the decoder.
// LZW codes remain the decoder's responsibility; this PoC only accepts complete GIFs.
static bool ValidateGif(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height) {
    if (size < 14 || (std::memcmp(data, "GIF87a", 6) && std::memcmp(data, "GIF89a", 6)))
        return false;
    auto u16 = [&](size_t i) { return static_cast<unsigned>(data[i] | (data[i + 1] << 8)); };
    width = static_cast<uint16_t>(u16(6));
    height = static_cast<uint16_t>(u16(8));
    if (!width || !height || width > 240 || height > 240)
        return false;
    size_t pos = 13;
    auto skip = [&](size_t n) {
        if (n > size - pos)
            return false;
        pos += n;
        return true;
    };
    auto blocks = [&]() {
        while (pos < size) {
            const size_t n = data[pos++];
            if (!n)
                return true;
            if (!skip(n))
                return false;
        }
        return false;
    };
    if ((data[10] & 0x80) && !skip(3u << ((data[10] & 7) + 1)))
        return false;
    bool has_frame = false;
    while (pos < size) {
        const uint8_t tag = data[pos++];
        if (tag == 0x3b)
            return has_frame && pos == size;
        if (tag == 0x21) {
            if (!skip(1) || !blocks())
                return false;
            continue;
        }
        if (tag != 0x2c || size - pos < 9)
            return false;
        const unsigned x = u16(pos), y = u16(pos + 2), w = u16(pos + 4), h = u16(pos + 6);
        const uint8_t flags = data[pos + 8];
        pos += 9;
        if (!w || !h || x + w > width || y + h > height ||
            ((flags & 0x80) && !skip(3u << ((flags & 7) + 1))) || !skip(1) || !blocks())
            return false;
        has_frame = true;
    }
    return false;
}

std::string FaceThemeService::LoadGifPair(FaceTheme& theme) {
    constexpr size_t kMaxGifBytes = 512 * 1024;
    auto pair = std::make_shared<FaceGifPair>();
    auto load = [&](const char* path, FaceGifData& output) -> std::string {
        auto* buffer = static_cast<uint8_t*>(
            heap_caps_malloc(kMaxGifBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer)
            return "Không đủ PSRAM để tải GIF.";
        std::unique_ptr<uint8_t, decltype(&heap_caps_free)> owner(buffer, &heap_caps_free);
        size_t used = 0;
        bool too_large = false;
        const auto result = storage_.Download(path, [&](const char* data, size_t size) {
            if (size > kMaxGifBytes - used) {
                too_large = true;
                return false;
            }
            std::memcpy(buffer + used, data, size);
            used += size;
            return true;
        });
        if (!result)
            return too_large ? "GIF vượt 512 KiB." : result.message;
        if (!ValidateGif(buffer, used, output.width, output.height))
            return "GIF không hợp lệ hoặc vượt 240×240.";
        auto* raw = owner.release();
        auto* resized = static_cast<uint8_t*>(
            heap_caps_realloc(raw, used, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (resized != nullptr)
            raw = resized;
        output.bytes = std::shared_ptr<uint8_t>(raw, [](uint8_t* p) { heap_caps_free(p); });
        output.size = used;
        return {};
    };
    auto error = load("/robot/themes/gif-poc/idle.gif", pair->idle);
    if (!error.empty())
        return "idle.gif: " + error;
    error = load("/robot/themes/gif-poc/talking.gif", pair->talking);
    if (!error.empty())
        return "talking.gif: " + error;
    ESP_LOGI("FaceGifPoC",
             "Loaded idle=%u bytes %ux%u, talking=%u bytes %ux%u; PSRAM free=%u, internal free=%u",
             static_cast<unsigned>(pair->idle.size), pair->idle.width, pair->idle.height,
             static_cast<unsigned>(pair->talking.size), pair->talking.width, pair->talking.height,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    theme.gifs = std::move(pair);
    return {};
}

std::string FaceThemeService::Select(const std::string& id) {
    FaceTheme theme;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = std::find_if(catalog_.begin(), catalog_.end(),
                                  [&](const Entry& e) { return e.theme.id == id; });
        if (found == catalog_.end())
            return "Không tìm thấy theme; hãy tải lại danh sách và chọn đúng ID.";
        theme = found->theme;
    }
    if (theme.style == "gif-poc") {
        auto error = LoadGifPair(theme);
        if (!error.empty())
            return error;
    }
    const auto json = EncodeFaceTheme(theme);
    if (json.empty() || settings_.SaveFaceTheme(json) != ESP_OK)
        return "Không lưu được theme; giữ lựa chọn hiện tại.";
    apply_(theme);
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = std::move(theme);
    return {};
}

void FaceThemeService::Run() {
    bool initial_scan = false;
    while (!stop_.load()) {
        std::string action, id, manifest;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            action.swap(action_);
            id.swap(id_);
            manifest.swap(manifest_);
        }
        if (!action.empty()) {
            std::string error;
            if (action == "refresh")
                error = Refresh();
            else if (action == "install")
                error = Install(manifest);
            else
                error = Select(id);
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = error;
            if (action != "refresh")
                message_ = error.empty() ? "Đã hoàn tất" : error;
            busy_ = false;
        } else if (!initial_scan && storage_.GetStatus().state == "mounted") {
            Refresh();
            FaceTheme restore;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initial_scan = sd_available_;
                restore = active_;
            }
            if (initial_scan && restore.style == "gif-poc") {
                auto error = LoadGifPair(restore);
                if (error.empty()) {
                    apply_(restore);
                    std::lock_guard<std::mutex> lock(mutex_);
                    active_ = std::move(restore);
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    error_ = "Không tải được GIF sau khởi động: " + error;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
}  // namespace kynangso
