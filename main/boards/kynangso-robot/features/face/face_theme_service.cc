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
constexpr size_t kMaxBuiltInThemes = 7;
constexpr size_t kMaxInternalThemes = 4;
constexpr size_t kMaxThemeCatalog = kMaxUserThemes + kMaxInternalThemes + kMaxBuiltInThemes;
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
        {{"prism", "Prism Reactor", "plasma", 0xFF76D8, 0x62F4FF, 0xFFD166, 0x17091D}, true}};
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
    if (ParseFaceTheme(settings_.LoadFaceTheme(), saved, error)) {
        if (saved.style == "gif-poc") {
            saved = FaceTheme{};
            const auto migrated = EncodeFaceTheme(saved);
            if (migrated.empty() || settings_.SaveFaceTheme(migrated) != ESP_OK)
                ESP_LOGW("FaceTheme", "Could not persist migration from the removed two-GIF PoC theme");
            else
                ESP_LOGI("FaceTheme", "Migrated removed two-GIF PoC theme to Classic Cyan");
        }
        active_ = std::move(saved);
    }
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
    if ((action != "select" && action != "refresh" && action != "install" && action != "delete") ||
        id.size() > 32 || manifest.size() > 2048)
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
    xTaskNotifyGive(worker_);
    return true;
}

void FaceThemeService::RequestEmotion(const char* emotion) {
    const int expression_index = CanonicalFaceGifExpression(emotion);
    if (expression_index < 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    const char* expression = FaceGifExpressionName(static_cast<size_t>(expression_index));
    if (requested_expression_ == expression)
        return;
    requested_expression_ = expression;
    ++gif_request_generation_;
    if (worker_ != nullptr)
        xTaskNotifyGive(worker_);
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
        cJSON_AddStringToObject(item, "storage",
                                entry.builtin ? "builtin"
                                              : (entry.internal_storage ? "internal" : "sd"));
        cJSON_AddItemToArray(items, item);
    }
    return root;
}

std::string FaceThemeService::Refresh() {
    SdStorage::Listing listing;
    const bool sd_mounted = storage_.GetStatus().state == "mounted";
    const auto listed = sd_mounted
                            ? storage_.List("/robot/themes", 0, listing)
                            : SdStorage::Result{SdStorage::Code::Unavailable,
                                                "Thẻ microSD chưa sẵn sàng."};
    std::vector<Entry> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : catalog_)
            if (entry.builtin)
                next.push_back(entry);
    }
    unsigned skipped = 0;
    size_t sd_theme_count = 0;
    bool truncated = listing.more;
    for (const auto& file : listing.entries) {
        if (file.directory || file.name.size() < 6 ||
            file.name.substr(file.name.size() - 5) != ".json")
            continue;
        if (sd_theme_count >= kMaxUserThemes || next.size() >= kMaxThemeCatalog) {
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
        ++sd_theme_count;
    }
    for (const auto& manifest : settings_.LoadLocalFaceThemes()) {
        if (next.size() >= kMaxThemeCatalog) {
            truncated = true;
            break;
        }
        FaceTheme theme;
        std::string error;
        if (!ParseFaceTheme(manifest, theme, error) || theme.HasGifExpressions() ||
            std::any_of(next.begin(), next.end(),
                        [&](const Entry& entry) { return entry.theme.id == theme.id; })) {
            ++skipped;
            continue;
        }
        next.push_back({std::move(theme), false, true});
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // A busy SD card must not erase a previously loaded catalog.
    if (!sd_mounted || listed || listed.code == SdStorage::Code::NotFound)
        catalog_ = std::move(next);
    sd_available_ = sd_mounted &&
                    (static_cast<bool>(listed) || listed.code == SdStorage::Code::NotFound);
    skipped_ = skipped;
    truncated_ = truncated;
    message_ = sd_available_ ? "Đã tải danh sách theme"
                             : "Đang dùng theme có sẵn và bộ nhớ robot";
    if (!sd_mounted || listed || listed.code == SdStorage::Code::NotFound)
        return {};
    return listed.message;
}

std::string FaceThemeService::Install(const std::string& manifest) {
    FaceTheme theme;
    std::string error;
    if (!ParseFaceTheme(manifest, theme, error))
        return error;
    const auto sd_status = storage_.GetStatus();
    const bool sd_mounted = sd_status.state == "mounted";
    if (theme.HasGifExpressions() && !sd_mounted)
        return "Theme GIF cần microSD; theme JSON đơn giản có thể lưu trong bộ nhớ robot.";
    if (!sd_mounted && sd_status.state != "unmounted")
        return "microSD chưa sẵn sàng: " + sd_status.message + " Hãy kết nối hoặc tháo thẻ an toàn rồi lưu lại.";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : catalog_)
            if (entry.theme.id == theme.id)
                return "ID theme đã tồn tại; hãy chọn ID mới.";
        const auto sd_theme_count = std::count_if(catalog_.begin(), catalog_.end(),
                                                  [](const Entry& entry) {
                                                      return !entry.builtin && !entry.internal_storage;
                                                  });
        const auto internal_theme_count = std::count_if(
            catalog_.begin(), catalog_.end(),
            [](const Entry& entry) { return entry.internal_storage; });
        if (sd_mounted && sd_theme_count >= kMaxUserThemes)
            return "Kho thẻ nhớ đã đủ 16 theme. Hãy xóa bớt theme trên thẻ rồi thử lại.";
        if (!sd_mounted && internal_theme_count >= kMaxInternalThemes)
            return "Bộ nhớ robot lưu tối đa 4 theme JSON. Hãy xóa theme rồi thử lại.";
    }
    const std::string canonical = EncodeFaceTheme(theme);
    if (canonical.empty())
        return "Không đủ bộ nhớ để tạo theme.";
    if (!sd_mounted) {
        const auto saved = settings_.SaveLocalFaceTheme(canonical);
        if (saved == ESP_ERR_INVALID_STATE)
            return "ID theme đã tồn tại trong bộ nhớ robot; hãy chọn ID mới.";
        if (saved == ESP_ERR_NO_MEM)
            return "Bộ nhớ robot đã đủ 4 theme JSON. Hãy xóa theme rồi thử lại.";
        if (saved != ESP_OK)
            return "Không lưu được theme trong bộ nhớ robot: " + std::string(esp_err_to_name(saved));
        return Refresh();
    }
    for (const char* path : {"/robot", "/robot/themes"}) {
        auto result = storage_.MakeDirectory(path);
        if (!result && result.code != SdStorage::Code::Exists)
            return result.message;
    }
    if (theme.HasGifExpressions()) {
        const auto directory = "/robot/themes/" + theme.id;
        auto result = storage_.MakeDirectory(directory);
        if (!result && result.code != SdStorage::Code::Exists)
            return result.message;
        const auto files = ValidateGifThemeFiles(theme, error);
        if (files != GifLoadResult::Ok)
            return error.empty() ? "Chưa thể kiểm tra file GIF; thử lại sau." : error;
    }
    auto result = storage_.Upload("/robot/themes/" + theme.id + ".json", 0, canonical.size(),
                                  canonical.data(), canonical.size());
    if (!result)
        return result.message;
    return Refresh();
}

std::string FaceThemeService::Delete(const std::string& id) {
    if (id.empty() || id.size() > 32)
        return "ID theme không hợp lệ.";
    for (const unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return "ID theme không hợp lệ.";

    bool internal_storage = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(catalog_.begin(), catalog_.end(), [&](const Entry& entry) {
            return entry.theme.id == id && !entry.builtin;
        });
        if (found == catalog_.end())
            return "Chỉ có thể xóa theme tùy chỉnh.";
        if (active_.id == id)
            return "Hãy chuyển sang theme khác trước khi xóa theme đang dùng.";
        internal_storage = found->internal_storage;
    }

    if (internal_storage) {
        const auto removed = settings_.DeleteLocalFaceTheme(id);
        if (removed != ESP_OK)
            return "Không xóa được theme trong bộ nhớ robot: " + std::string(esp_err_to_name(removed));
        return Refresh();
    }

    auto directory = storage_.Remove("/robot/themes/" + id);
    if (!directory && directory.code != SdStorage::Code::NotFound)
        return directory.message;
    auto manifest = storage_.Remove("/robot/themes/" + id + ".json");
    if (!manifest && manifest.code != SdStorage::Code::NotFound)
        return manifest.message;
    if (!directory && !manifest)
        return "Không tìm thấy dữ liệu theme trên thẻ nhớ.";
    return Refresh();
}

// Validate the block structure before exposing untrusted card data to the decoder.
// LZW codes remain the decoder's responsibility; this PoC only accepts complete GIFs.
static bool ValidateGif(const uint8_t* data, size_t size, uint16_t max_dimension,
                        uint16_t max_frames, uint16_t& width, uint16_t& height,
                        uint16_t& frame_count) {
    frame_count = 0;
    if (size < 14 || (std::memcmp(data, "GIF87a", 6) && std::memcmp(data, "GIF89a", 6)))
        return false;
    auto u16 = [&](size_t i) { return static_cast<unsigned>(data[i] | (data[i + 1] << 8)); };
    width = static_cast<uint16_t>(u16(6));
    height = static_cast<uint16_t>(u16(8));
    if (!width || !height || width > max_dimension || height > max_dimension)
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
        if (++frame_count > max_frames)
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
        if (!ValidateGif(buffer, used, 240, 512, output.width, output.height, output.frame_count))
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

FaceThemeService::GifLoadResult FaceThemeService::LoadGifExpression(
    const FaceTheme& theme, const std::string& expression,
    std::shared_ptr<const FaceGifData>& output, std::string& error) {
    constexpr size_t kMaxGifBytes = 256 * 1024;
    const int expression_index = FaceGifExpressionIndex(expression.c_str());
    if (!theme.HasGifExpressions() || expression_index < 0 ||
        !theme.gif_expressions[expression_index])
        return GifLoadResult::Error;

    const auto directory = "/robot/themes/" + theme.id;
    SdStorage::Listing listing;
    const auto listed = storage_.List(directory, 0, listing);
    if (!listed) {
        error = listed.message;
        return listed.code == SdStorage::Code::Busy ? GifLoadResult::Busy : GifLoadResult::Error;
    }
    if (listing.more) {
        error = "Thư mục theme có quá nhiều file.";
        return GifLoadResult::Error;
    }
    const std::string filename = expression + ".gif";
    const auto file = std::find_if(
        listing.entries.begin(), listing.entries.end(),
        [&](const SdStorage::Entry& item) { return !item.directory && item.name == filename; });
    if (file == listing.entries.end() || file->size == 0 || file->size > kMaxGifBytes) {
        error = "GIF biểu cảm thiếu hoặc vượt 256 KiB: " + filename;
        return GifLoadResult::Error;
    }
    const size_t file_size = static_cast<size_t>(file->size);
    auto* buffer =
        static_cast<uint8_t*>(heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) {
        error = "Không đủ PSRAM để nạp GIF biểu cảm.";
        return GifLoadResult::Error;
    }
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> owner(buffer, &heap_caps_free);
    size_t used = 0;
    bool too_large = false;
    const auto path = directory + "/" + filename;
    const auto result = storage_.Download(path, [&](const char* data, size_t size) {
        if (size > file_size - used) {
            too_large = true;
            return false;
        }
        std::memcpy(buffer + used, data, size);
        used += size;
        return true;
    });
    if (!result) {
        error = too_large ? "GIF biểu cảm vượt 256 KiB." : result.message;
        return result.code == SdStorage::Code::Busy ? GifLoadResult::Busy : GifLoadResult::Error;
    }
    if (used != file_size) {
        error = "Kích thước GIF thay đổi trong lúc đọc; hãy tải lại danh sách theme.";
        return GifLoadResult::Error;
    }

    auto data = std::make_shared<FaceGifData>();
    if (!ValidateGif(buffer, used, 160, 60, data->width, data->height, data->frame_count)) {
        error = "GIF không hợp lệ hoặc vượt giới hạn 160×160/60 khung hình.";
        return GifLoadResult::Error;
    }
    auto* raw = owner.release();
    data->bytes = std::shared_ptr<uint8_t>(raw, [](uint8_t* p) { heap_caps_free(p); });
    data->size = used;
    output = std::move(data);
    error.clear();
    return GifLoadResult::Ok;
}

FaceThemeService::GifLoadResult FaceThemeService::ValidateGifThemeFiles(const FaceTheme& theme,
                                                                        std::string& error) {
    SdStorage::Listing listing;
    const auto listed = storage_.List("/robot/themes/" + theme.id, 0, listing);
    if (!listed) {
        error = listed.message;
        return listed.code == SdStorage::Code::Busy ? GifLoadResult::Busy : GifLoadResult::Error;
    }
    if (listing.more) {
        error = "Thư mục theme có quá nhiều file.";
        return GifLoadResult::Error;
    }
    for (size_t i = 0; i < kFaceGifExpressionCount; ++i) {
        if (!theme.gif_expressions[i])
            continue;
        const std::string filename = std::string(FaceGifExpressionName(i)) + ".gif";
        auto found = std::find_if(listing.entries.begin(), listing.entries.end(),
                                  [&](const SdStorage::Entry& item) {
                                      return !item.directory && item.name == filename &&
                                             item.size > 0 && item.size <= 256 * 1024;
                                  });
        if (found == listing.entries.end()) {
            error = "Thiếu GIF hoặc file vượt 256 KiB: " + filename;
            return GifLoadResult::Error;
        }
    }
    error.clear();
    return GifLoadResult::Ok;
}

std::shared_ptr<const FaceGifData> FaceThemeService::FindCachedGif(const std::string& theme_id,
                                                                   const std::string& expression) {
    for (auto& entry : gif_cache_) {
        if (entry.data && entry.theme_id == theme_id && entry.expression == expression) {
            entry.last_used = ++gif_cache_clock_;
            return entry.data;
        }
    }
    return {};
}

void FaceThemeService::CacheGif(const std::string& theme_id, const std::string& expression,
                                std::shared_ptr<const FaceGifData> data) {
    size_t target = 0;
    for (size_t i = 0; i < gif_cache_.size(); ++i) {
        if (!gif_cache_[i].data ||
            (gif_cache_[i].theme_id == theme_id && gif_cache_[i].expression == expression)) {
            target = i;
            break;
        }
        if (gif_cache_[i].last_used < gif_cache_[target].last_used)
            target = i;
    }
    gif_cache_[target] = {theme_id, expression, std::move(data), ++gif_cache_clock_};
}

void FaceThemeService::ClearGifCache() {
    for (auto& entry : gif_cache_)
        entry = {};
    gif_cache_clock_ = 0;
}

void FaceThemeService::EvictInactiveGifCache(const std::string& theme_id,
                                            const std::string& active_expression) {
    for (auto& entry : gif_cache_)
        if (entry.data && entry.theme_id == theme_id && entry.expression != active_expression)
            entry = {};
}

std::string FaceThemeService::Select(const std::string& id) {
    FaceTheme theme;
    std::string requested_expression;
    std::string previous_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = std::find_if(catalog_.begin(), catalog_.end(),
                                  [&](const Entry& e) { return e.theme.id == id; });
        if (found == catalog_.end())
            return "Không tìm thấy theme; hãy tải lại danh sách và chọn đúng ID.";
        theme = found->theme;
        requested_expression = requested_expression_;
        previous_id = active_.id;
    }
    if (previous_id != id)
        ClearGifCache();
    if (theme.style == "gif-poc") {
        theme.active_gif.reset();
        theme.active_gif_expression.clear();
        auto error = LoadGifPair(theme);
        if (!error.empty())
            return error;
    } else if (theme.HasGifExpressions()) {
        std::string error;
        const auto files = ValidateGifThemeFiles(theme, error);
        if (files != GifLoadResult::Ok)
            return error.empty() ? "Chưa thể kiểm tra file GIF; thử lại sau." : error;

        int expression_index = FaceGifExpressionIndex(requested_expression.c_str());
        if (expression_index < 0 || !theme.gif_expressions[expression_index]) {
            expression_index = FaceGifExpressionIndex("neutral");
            if (expression_index < 0 || !theme.gif_expressions[expression_index]) {
                for (size_t i = 0; i < kFaceGifExpressionCount; ++i)
                    if (theme.gif_expressions[i]) {
                        expression_index = static_cast<int>(i);
                        break;
                    }
            }
        }
        if (expression_index >= 0 && theme.gif_expressions[expression_index]) {
            requested_expression = FaceGifExpressionName(static_cast<size_t>(expression_index));
            std::shared_ptr<const FaceGifData> data =
                previous_id == theme.id ? FindCachedGif(theme.id, requested_expression)
                                        : std::shared_ptr<const FaceGifData>{};
            if (!data) {
                const auto loaded = LoadGifExpression(theme, requested_expression, data, error);
                if (loaded != GifLoadResult::Ok)
                    return error.empty() ? "Không tải được GIF biểu cảm." : error;
                CacheGif(theme.id, requested_expression, data);
            }
            theme.active_gif = std::move(data);
            theme.active_gif_expression = requested_expression;
        }
    } else {
        theme.active_gif.reset();
        theme.active_gif_expression.clear();
    }
    const auto json = EncodeFaceTheme(theme);
    if (json.empty() || settings_.SaveFaceTheme(json) != ESP_OK)
        return "Không lưu được theme; giữ lựa chọn hiện tại.";
    apply_(theme);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = theme;
    }
    if (theme.active_gif && theme.HasGifExpressions())
        CacheGif(theme.id, theme.active_gif_expression, theme.active_gif);
    return {};
}

bool FaceThemeService::ApplyRequestedGif(uint32_t generation, const std::string& expression) {
    FaceTheme theme;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != gif_request_generation_)
            return false;
        theme = active_;
    }
    if (!theme.HasGifExpressions() || theme.active_gif_expression == expression)
        return true;
    if (storage_.IsAudioPlaybackActive())
        return false;

    std::shared_ptr<const FaceGifData> data;
    std::string error;
    const int expression_index = FaceGifExpressionIndex(expression.c_str());
    if (expression_index >= 0 && theme.gif_expressions[expression_index]) {
        data = FindCachedGif(theme.id, expression);
        if (!data) {
            EvictInactiveGifCache(theme.id, theme.active_gif_expression);
            const auto loaded = LoadGifExpression(theme, expression, data, error);
            if (loaded == GifLoadResult::Busy)
                return false;
            if (loaded == GifLoadResult::Ok) {
                CacheGif(theme.id, expression, data);
            } else {
                ESP_LOGW("FaceTheme", "GIF %s/%s unavailable; procedural fallback: %s",
                         theme.id.c_str(), expression.c_str(), error.c_str());
            }
        }
    }

    theme.active_gif = std::move(data);
    theme.active_gif_expression = expression;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != gif_request_generation_ || active_.id != theme.id)
            return false;
        active_ = theme;
    }
    if (theme.active_gif)
        ESP_LOGI("FaceTheme", "Loaded %s/%s GIF: %u bytes, %ux%u, %u frames; PSRAM free=%u",
                 theme.id.c_str(), expression.c_str(),
                 static_cast<unsigned>(theme.active_gif->size), theme.active_gif->width,
                 theme.active_gif->height, theme.active_gif->frame_count,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    apply_(theme);
    return true;
}

void FaceThemeService::Run() {
    bool initial_scan = false;
    bool catalog_loaded = false;
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
            else if (action == "delete")
                error = Delete(id);
            else
                error = Select(id);
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = error;
            if (action != "refresh")
                message_ = error.empty() ? "Đã hoàn tất" : error;
            busy_ = false;
        } else if (!catalog_loaded ||
                   (!initial_scan && storage_.GetStatus().state == "mounted")) {
            const bool sd_mounted = storage_.GetStatus().state == "mounted";
            Refresh();
            catalog_loaded = true;
            FaceTheme restore;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initial_scan = initial_scan || (sd_mounted && sd_available_);
                restore = active_;
            }
            if (sd_mounted && initial_scan && restore.style == "gif-poc") {
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
        FaceTheme active;
        std::string expression;
        uint32_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active = active_;
            expression = requested_expression_;
            generation = gif_request_generation_;
        }
        if (active.HasGifExpressions() && storage_.GetStatus().state == "mounted")
            ApplyRequestedGif(generation, expression);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}
}  // namespace kynangso
