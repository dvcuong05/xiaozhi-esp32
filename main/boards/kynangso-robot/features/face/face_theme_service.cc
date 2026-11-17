#include "features/face/face_theme_service.h"
#include <algorithm>
#include "core/robot_settings.h"
#include "features/storage/sd_storage.h"

namespace kynangso {
namespace {
constexpr size_t kMaxUserThemes = 16;
constexpr size_t kMaxBuiltInThemes = 7;
constexpr size_t kMaxThemeCatalog = kMaxUserThemes + kMaxBuiltInThemes;
}  // namespace

FaceThemeService::FaceThemeService(RobotSettings& settings, SdStorage& storage,
                                   std::function<void(const FaceTheme&)> apply)
    : settings_(settings), storage_(storage), apply_(std::move(apply)) {
    catalog_ = {{{}, true},
                {{"candy", "Candy Rose", "soft", 0xFF8AC6, 0xFFD166, 0x92E7FF, 0x140D22}, true},
                {{"neon", "Neon Violet", "rounded", 0xB794FF, 0x4DFFD2, 0x55D8FF, 0x080C20}, true},
                {{"pixel", "Pixel Mint", "pixel", 0x66FFB2, 0xFFCF5C, 0x73CFFF, 0x03130F}, true},
                {{"aether", "Aether Hologram", "holo", 0x75F5FF, 0xA889FF, 0x48D7FF, 0x041326},
                 true},
                {{"orbit", "Quantum Orbit", "orbital", 0xA28BFF, 0xFF54D9, 0x62EFFF, 0x08091D},
                 true},
                {{"prism", "Prism Reactor", "plasma", 0xFF76D8, 0x62F4FF, 0xFFD166, 0x17091D},
                 true}};
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
            std::lock_guard<std::mutex> lock(mutex_);
            initial_scan = sd_available_;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
}  // namespace kynangso
