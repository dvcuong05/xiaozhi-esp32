#include "robot_time_service.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "application.h"

namespace mrkenddo {
namespace {
constexpr int64_t kValidTime = 1704067200;
constexpr int64_t kZone = 7 * 3600;  // Product timezone: Asia/Ho_Chi_Minh, no DST.
int64_t CalendarUtc(int year, int month, int day, int hour, int minute, int second = 0) {
    const int adjusted = year - (month <= 2);
    const int era = adjusted / 400;
    const unsigned yoe = adjusted - era * 400;
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t days = era * 146097LL + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second - kZone;
}
int64_t ClockNow() {
    // Ota::CheckVersion installs timestamp + timezone_offset in the system
    // clock. Read the same local calendar as the OLED, then normalize it once
    // to UTC for persisted due dates and the web API's Unix timestamps.
    const time_t system_time = time(nullptr);
    tm local{};
    if (!localtime_r(&system_time, &local))
        return 0;
    return CalendarUtc(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                       local.tm_min, local.tm_sec);
}
int64_t Mono() { return esp_timer_get_time() / 1000000; }
std::string Text(const cJSON* j, const char* key) {
    auto* v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
int Number(const cJSON* j, const char* key, int fallback, int low, int high) {
    auto* v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v)
        return fallback;
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) ||
        std::floor(v->valuedouble) != v->valuedouble || v->valuedouble < low ||
        v->valuedouble > high)
        return -1;
    return static_cast<int>(v->valuedouble);
}
std::string Print(cJSON* j) {
    char* raw = cJSON_PrintUnformatted(j);
    std::string result = raw ? raw : "{\"ok\":false,\"error\":\"Out of memory\"}";
    cJSON_free(raw);
    cJSON_Delete(j);
    return result;
}
std::string LocalTime(int64_t epoch) {
    time_t local = epoch + kZone;
    tm t{};
    gmtime_r(&local, &t);
    char s[32];
    strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &t);
    return s;
}
bool ParseLocalDateTime(const std::string& value, int64_t* epoch) {
    int year, month, day, hour, minute, used = 0;
    if (value.size() != 16 ||
        sscanf(value.c_str(), "%d-%d-%dT%d:%d%n", &year, &month, &day, &hour, &minute, &used) !=
            5 ||
        used != static_cast<int>(value.size()) || year < 2024 || year > 2099 || month < 1 ||
        month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;
    const int64_t parsed = CalendarUtc(year, month, day, hour, minute);
    char expected[24];
    snprintf(expected, sizeof(expected), "%04d-%02d-%02d %02d:%02d", year, month, day, hour,
             minute);
    if (LocalTime(parsed).substr(0, 16) != expected)
        return false;
    *epoch = parsed;
    return true;
}
bool ValidTitle(const std::string& s) {
    return s.size() <= 96 &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
}  // namespace

RobotTimeService::RobotTimeService(Present present) : present_(std::move(present)) {}
RobotTimeService::~RobotTimeService() { Stop(); }
bool RobotTimeService::Start() {
    Load();
    stopped_ = xSemaphoreCreateBinary();
    if (!stopped_)
        return false;
    if (xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<RobotTimeService*>(arg);
                while (!self->stopping_) {
                    self->Tick();
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
                }
                xSemaphoreGive(self->stopped_);
                vTaskDelete(nullptr);
            },
            "robot_time", 6144, this, 1, &task_) != pdPASS) {
        vSemaphoreDelete(stopped_);
        stopped_ = nullptr;
        return false;
    }
    return true;
}
void RobotTimeService::Stop() {
    stopping_ = true;
    if (task_) {
        xTaskNotifyGive(task_);
        xSemaphoreTake(stopped_, portMAX_DELAY);
        task_ = nullptr;
    }
    if (stopped_) {
        vSemaphoreDelete(stopped_);
        stopped_ = nullptr;
    }
}

cJSON* RobotTimeService::EntriesJson(const char* scope, bool include_details) const {
    auto* array = cJSON_CreateArray();
    std::array<const Entry*, 8> sorted{};
    std::size_t count = 0;
    const auto now = ClockNow();
    const auto today = LocalTime(now).substr(0, 10);
    for (const auto& entry : entries_) {
        if (!entry.id)
            continue;
        if (scope && std::strcmp(scope, "today") == 0 &&
            (!entry.enabled || LocalTime(entry.due).substr(0, 10) != today))
            continue;
        if (scope && std::strcmp(scope, "next") == 0 && (!entry.enabled || entry.due <= now))
            continue;
        sorted[count++] = &entry;
    }
    if (include_details) {
        const auto precedes = [](const Entry* left, const Entry* right) {
            if (left->enabled != right->enabled)
                return left->enabled;
            return left->due < right->due;
        };
        for (std::size_t index = 1; index < count; ++index) {
            const Entry* current = sorted[index];
            std::size_t position = index;
            while (position > 0 && precedes(current, sorted[position - 1])) {
                sorted[position] = sorted[position - 1];
                --position;
            }
            sorted[position] = current;
        }
    }
    const std::size_t limit =
        scope && std::strcmp(scope, "next") == 0 ? std::min<std::size_t>(1, count) : count;
    for (std::size_t index = 0; index < limit; ++index) {
        const auto* entry = sorted[index];
        const auto& e = *entry;
        auto* j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "id", e.id);
        cJSON_AddStringToObject(j, "kind", e.alarm ? "alarm" : "reminder");
        cJSON_AddStringToObject(j, "title", e.title.c_str());
        cJSON_AddBoolToObject(j, "enabled", e.enabled);
        cJSON_AddNumberToObject(j, "due", e.due);
        cJSON_AddNumberToObject(j, "hour", e.hour);
        cJSON_AddNumberToObject(j, "minute", e.minute);
        cJSON_AddNumberToObject(j, "days", e.days);
        if (include_details) {
            cJSON_AddStringToObject(j, "due_local", LocalTime(e.due).c_str());
            cJSON_AddBoolToObject(j, "recurring", e.days != 0);
        }
        cJSON_AddItemToArray(array, j);
    }
    return array;
}
bool RobotTimeService::Save() {
    const auto json = Print(EntriesJson(nullptr, false));
    if (json.empty() || json.front() != '[') {
        storage_ok_ = false;
        return false;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open("robot_time", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "entries_v1", json.c_str());
        if (result == ESP_OK)
            result = nvs_commit(handle);
        nvs_close(handle);
    }
    storage_ok_ = result == ESP_OK;
    if (!storage_ok_)
        ESP_LOGE("RobotTime", "Cannot persist schedules: %s", esp_err_to_name(result));
    return storage_ok_;
}
void RobotTimeService::Load() {
    nvs_handle_t handle;
    auto err = nvs_open("robot_time", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return;
    if (err != ESP_OK) {
        storage_ok_ = false;
        return;
    }
    size_t size = 0;
    err = nvs_get_str(handle, "entries_v1", nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return;
    }
    if (err != ESP_OK || size > 4096 || size < 3) {
        storage_ok_ = false;
        nvs_close(handle);
        return;
    }
    std::string raw(size, '\0');
    err = nvs_get_str(handle, "entries_v1", raw.data(), &size);
    nvs_close(handle);
    auto* root = err == ESP_OK ? cJSON_Parse(raw.c_str()) : nullptr;
    storage_ok_ = cJSON_IsArray(root) && cJSON_GetArraySize(root) <= 8;
    if (storage_ok_) {
        int index = 0;
        cJSON* j;
        cJSON_ArrayForEach (j, root) {
            Entry e;
            e.id = Number(j, "id", -1, 1, 8);
            e.hour = Number(j, "hour", -1, 0, 23);
            e.minute = Number(j, "minute", -1, 0, 59);
            e.days = Number(j, "days", -1, 0, 127);
            e.title = Text(j, "title");
            const auto kind = Text(j, "kind");
            auto* due = cJSON_GetObjectItemCaseSensitive(j, "due");
            if (e.id < 0 || e.hour < 0 || e.minute < 0 || e.days < 0 || !ValidTitle(e.title) ||
                (kind != "alarm" && kind != "reminder") || !cJSON_IsNumber(due) ||
                !std::isfinite(due->valuedouble) || due->valuedouble < kValidTime ||
                due->valuedouble > 4102444800LL ||
                std::any_of(entries_.begin(), entries_.end(),
                            [&](const Entry& x) { return x.id == e.id; })) {
                storage_ok_ = false;
                break;
            }
            e.alarm = kind == "alarm";
            e.enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "enabled"));
            e.due = static_cast<int64_t>(due->valuedouble);
            entries_[index++] = std::move(e);
        }
    }
    if (!storage_ok_)
        entries_ = {};
    cJSON_Delete(root);
}
int64_t RobotTimeService::NextAlarm(const Entry& e, int64_t after) const {
    const int64_t day = (after + kZone) / 86400;
    for (int offset = 0; offset <= 7; ++offset) {
        int64_t due = (day + offset) * 86400 - kZone + e.hour * 3600 + e.minute * 60;
        int weekday = static_cast<int>((day + offset + 4) % 7);
        if (due > after && (!e.days || (e.days & (1 << weekday))))
            return due;
    }
    return 0;
}
void RobotTimeService::PushAlert(int id, const std::string& title, const char* mode,
                                 TimeRingtone ringtone, int seconds) {
    if (alerts_.size() >= 10)
        alerts_.pop_front();
    alerts_.push_back({id, title, mode, 0, ++alert_serial_, seconds, ringtone});
    next_sound_ = 0;
}
bool RobotTimeService::DismissAlert() {
    std::lock_guard lock(mutex_);
    if (alerts_.empty())
        return false;
    alerts_.pop_front();
    next_sound_ = 0;
    if (task_)
        xTaskNotifyGive(task_);
    return true;
}

std::string RobotTimeService::Status(const char* error, const char* scope) const {
    auto* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", error == nullptr);
    if (error)
        cJSON_AddStringToObject(j, "error", error);
    cJSON_AddBoolToObject(j, "available", task_ != nullptr && !stopping_);
    const auto now = ClockNow();
    cJSON_AddBoolToObject(j, "clock_ready", now >= kValidTime);
    cJSON_AddBoolToObject(j, "storage_ok", storage_ok_);
    cJSON_AddStringToObject(j, "timezone", "Asia/Ho_Chi_Minh (UTC+07:00)");
    cJSON_AddStringToObject(j, "now", LocalTime(now).c_str());
    cJSON_AddItemToObject(j, "entries", EntriesJson(scope));
    cJSON_AddBoolToObject(j, "running", running_);
    cJSON_AddBoolToObject(j, "paused", paused_);
    cJSON_AddBoolToObject(j, "visible", visible_);
    cJSON_AddStringToObject(j, "mode", pomodoro_ ? (rest_ ? "break" : "focus") : "countdown");
    cJSON_AddStringToObject(j, "title", timer_title_.c_str());
    cJSON_AddNumberToObject(
        j, "remaining",
        running_ ? std::max<int64_t>(0, paused_ ? remaining_ : deadline_ - Mono()) : 0);
    cJSON_AddNumberToObject(j, "round", round_);
    cJSON_AddNumberToObject(j, "rounds", rounds_);
    cJSON_AddStringToObject(j, "alert", alerts_.empty() ? "" : alerts_.front().title.c_str());
    return Print(j);
}

std::string RobotTimeService::Command(const cJSON* j) {
    std::lock_guard lock(mutex_);
    const auto op = Text(j, "op");
    if (op == "status" || op == "list")
        return Status();
    if (op == "today" || op == "next") {
        if (ClockNow() < kValidTime)
            return Status("Chưa đồng bộ giờ mạng", op == "today" ? "today" : "next");
        return Status(nullptr, op == "today" ? "today" : "next");
    }
    if (!task_ || stopping_)
        return Status("Bộ hẹn giờ chưa sẵn sàng");
    if (op == "add") {
        if (ClockNow() < kValidTime)
            return Status("Chưa đồng bộ giờ mạng; hãy kết nối Wi-Fi trước");
        auto slot =
            std::find_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.id; });
        if (slot == entries_.end())
            return Status("Đã đủ 8 lịch; hãy xóa lịch không dùng");
        Entry e;
        e.id = 1;
        while (std::any_of(entries_.begin(), entries_.end(),
                           [&](const Entry& x) { return x.id == e.id; }))
            ++e.id;
        e.enabled = true;
        e.title = Text(j, "title");
        if (!ValidTitle(e.title))
            return Status("Nội dung tối đa 96 byte UTF-8, không có ký tự điều khiển");
        const auto kind = Text(j, "kind");
        if (kind != "alarm" && kind != "reminder")
            return Status("kind phải là alarm hoặc reminder");
        e.alarm = kind == "alarm";
        if (e.title.empty())
            e.title = e.alarm ? "Báo thức" : "Nhắc việc";
        if (e.alarm) {
            e.hour = Number(j, "hour", -1, 0, 23);
            e.minute = Number(j, "minute", -1, 0, 59);
            e.days = Number(j, "days", 0, 0, 127);
            if (e.hour < 0 || e.minute < 0 || e.days < 0)
                return Status("Giờ/phút/ngày lặp không hợp lệ");
            e.due = NextAlarm(e, ClockNow());
        } else {
            e.days = Number(j, "days", 0, 0, 127);
            if (e.days < 0)
                return Status("Ngày lặp không hợp lệ (0–127)");
            const auto at = Text(j, "at");
            int y, m, d, h, min, used = 0;
            if (at.size() != 16 ||
                sscanf(at.c_str(), "%d-%d-%dT%d:%d%n", &y, &m, &d, &h, &min, &used) != 5 ||
                used != static_cast<int>(at.size()) || y < 2024 || y > 2099 || m < 1 || m > 12 ||
                d < 1 || d > 31 || h < 0 || h > 23 || min < 0 || min > 59)
                return Status("Thời điểm cần dạng YYYY-MM-DDTHH:MM, giờ Việt Nam");
            e.due = CalendarUtc(y, m, d, h, min);
            char expected[24];
            snprintf(expected, sizeof(expected), "%04d-%02d-%02d %02d:%02d", y, m, d, h, min);
            if (LocalTime(e.due).substr(0, 16) != expected || e.due <= ClockNow())
                return Status("Ngày không hợp lệ hoặc đã qua");
        }
        if (!e.alarm) {
            const int64_t requested_due = e.due;
            time_t local_due = requested_due + kZone;
            tm local{};
            gmtime_r(&local_due, &local);
            e.hour = local.tm_hour;
            e.minute = local.tm_min;
            if (e.days)
                e.due = NextAlarm(e, requested_due - 60);
        }
        *slot = e;
        if (!Save()) {
            *slot = {};
            return Status("Không lưu được NVS; lịch chưa được tạo");
        }
    } else if (op == "rename" || op == "reschedule" || op == "repeat" || op == "skip") {
        const int id = Number(j, "id", -1, 1, 8);
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.id == id; });
        if (it == entries_.end())
            return Status("Không tìm thấy lịch");
        const auto old = *it;
        const int64_t now = ClockNow();
        if (op != "rename" && now < kValidTime)
            return Status("Chưa đồng bộ giờ mạng");
        bool clear_alert = false;
        if (op == "rename") {
            auto title = Text(j, "title");
            if (title.empty() || !ValidTitle(title))
                return Status("Tên lịch không hợp lệ; tối đa 96 byte UTF-8");
            it->title = std::move(title);
        } else if (op == "reschedule") {
            if (it->alarm) {
                const int hour = Number(j, "hour", -1, 0, 23);
                const int minute = Number(j, "minute", -1, 0, 59);
                if (hour < 0 || minute < 0)
                    return Status("Giờ/phút báo thức không hợp lệ");
                it->hour = hour;
                it->minute = minute;
                it->due = NextAlarm(*it, now);
            } else {
                int64_t requested_due;
                if (!ParseLocalDateTime(Text(j, "at"), &requested_due) || requested_due <= now)
                    return Status("Thời điểm không hợp lệ hoặc đã qua");
                time_t local_due = requested_due + kZone;
                tm local{};
                gmtime_r(&local_due, &local);
                it->hour = local.tm_hour;
                it->minute = local.tm_min;
                it->due = it->days ? NextAlarm(*it, requested_due - 60) : requested_due;
            }
            it->enabled = true;
            clear_alert = true;
        } else if (op == "repeat") {
            if (std::any_of(alerts_.begin(), alerts_.end(),
                            [id](const Alert& a) { return a.id == id; }))
                return Status("Lịch đang báo; hãy xác nhận hoặc nhắc lại trước");
            const int days = Number(j, "days", -1, 0, 127);
            if (days < 0)
                return Status("Ngày lặp không hợp lệ (0–127)");
            it->days = days;
            if (days) {
                const int64_t anchor = std::max(now, it->due - 60);
                it->due = NextAlarm(*it, anchor);
            } else if (it->due <= now) {
                *it = old;
                return Status("Lịch đã qua; hãy đổi giờ trước khi bỏ lặp");
            }
        } else {
            if (!it->days)
                return Status("Chỉ lịch lặp mới có lần kế tiếp để bỏ qua");
            if (!it->enabled)
                return Status("Lịch đang tắt; hãy bật lịch trước khi bỏ qua lần tới");
            if (std::any_of(alerts_.begin(), alerts_.end(),
                            [id](const Alert& a) { return a.id == id; }))
                return Status("Lịch đang báo; hãy xác nhận hoặc nhắc lại trước");
            it->due = NextAlarm(*it, std::max(now, it->due));
        }
        if (!Save()) {
            *it = old;
            return Status("Không lưu được thay đổi lịch vào NVS");
        }
        if (clear_alert)
            std::erase_if(alerts_, [id](const Alert& a) { return a.id == id; });
        if (op == "rename")
            for (auto& alert : alerts_)
                if (alert.id == id)
                    alert.title = it->title;
    } else if (op == "enable" || op == "disable") {
        int id = Number(j, "id", -1, 1, 8);
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.id == id; });
        if (it == entries_.end())
            return Status("Không tìm thấy lịch");
        auto old = *it;
        if (op == "enable") {
            if (ClockNow() < kValidTime)
                return Status("Chưa đồng bộ giờ");
            if (it->alarm || it->days)
                it->due = NextAlarm(*it, ClockNow());
            else if (it->due <= ClockNow())
                return Status("Nhắc việc đã qua; hãy tạo lịch mới");
        }
        it->enabled = op == "enable";
        if (!Save()) {
            *it = old;
            return Status("Không lưu được NVS");
        }
        if (!it->enabled)
            std::erase_if(alerts_, [id](const Alert& a) { return a.id == id; });
    } else if (op == "delete") {
        int id = Number(j, "id", -1, 1, 8);
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.id == id; });
        if (it == entries_.end())
            return Status("Không tìm thấy lịch");
        const auto old = *it;
        *it = {};
        if (!Save()) {
            *it = old;
            return Status("Không lưu được NVS");
        }
        std::erase_if(alerts_, [id](const Alert& a) { return a.id == id; });
    } else if (op == "countdown" || op == "pomodoro") {
        if (running_)
            return Status("Đang có phiên hẹn giờ; hãy dừng phiên trước");
        int seconds = Number(j, "seconds", 300, 1, 86400);
        int focus = Number(j, "focus_minutes", 25, 1, 180);
        int rest = Number(j, "break_minutes", 5, 1, 60);
        int longer = Number(j, "long_break_minutes", 15, 1, 120);
        int rounds = Number(j, "rounds", 4, 1, 12);
        auto title = Text(j, "title");
        if (seconds < 0 || focus < 0 || rest < 0 || longer < 0 || rounds < 0 || !ValidTitle(title))
            return Status("Thời lượng, số vòng hoặc nội dung không hợp lệ");
        focus_ = focus * 60;
        short_break_ = rest * 60;
        long_break_ = longer * 60;
        rounds_ = rounds;
        round_ = 1;
        rest_ = false;
        paused_ = false;
        pomodoro_ = op == "pomodoro";
        total_ = pomodoro_ ? focus_ : seconds;
        deadline_ = Mono() + total_;
        timer_title_ = title.empty() ? (pomodoro_ ? "Một việc mỗi lúc" : "Đếm ngược") : title;
        running_ = true;
        visible_ = true;
    } else if (op == "pause") {
        if (!running_ || paused_)
            return Status("Không có phiên đang chạy để tạm dừng");
        remaining_ = std::max<int64_t>(0, deadline_ - Mono());
        paused_ = true;
    } else if (op == "resume") {
        if (!running_ || !paused_)
            return Status("Không có phiên đang tạm dừng");
        deadline_ = Mono() + remaining_;
        paused_ = false;
    } else if (op == "stop") {
        running_ = false;
        paused_ = false;
        std::erase_if(alerts_, [](const Alert& a) { return a.id == 0; });
    } else if (op == "hide" || op == "show") {
        visible_ = op == "show";
    } else if (op == "dismiss") {
        if (!alerts_.empty())
            alerts_.pop_front();
    } else if (op == "snooze") {
        if (alerts_.empty())
            return Status("Không có thông báo để nhắc lại");
        if (ClockNow() < kValidTime)
            return Status("Chưa đồng bộ giờ");
        auto slot =
            std::find_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.id; });
        if (slot == entries_.end())
            return Status("Đã đủ 8 lịch; hãy xóa lịch không dùng");
        Entry e;
        e.id = 1;
        e.enabled = true;
        e.title = alerts_.front().title;
        while (std::any_of(entries_.begin(), entries_.end(),
                           [&](const Entry& x) { return x.id == e.id; }))
            ++e.id;
        e.due = ClockNow() + 300;
        *slot = e;
        if (!Save()) {
            *slot = {};
            return Status("Không lưu được nhắc lại");
        }
        alerts_.pop_front();
    } else
        return Status("Lệnh hẹn giờ không hợp lệ");
    if (task_)
        xTaskNotifyGive(task_);
    return Status();
}

void RobotTimeService::Tick() {
    {
        std::lock_guard lock(mutex_);
        int64_t now = ClockNow(), mono = Mono();
        bool changed = false;
        if (now >= kValidTime)
            for (auto& e : entries_) {
                if (!e.id || !e.enabled || e.due > now)
                    continue;
                if (now - e.due <= 600)
                    PushAlert(e.id, e.title, e.alarm ? "alarm" : "reminder",
                              e.alarm ? TimeRingtone::Alarm : TimeRingtone::Reminder);
                if (e.days)
                    e.due = NextAlarm(e, now);
                else
                    e.enabled = false;
                changed = true;
            }
        if (changed)
            Save();  // Worker only; never writes NVS from the audio/UI tick.
        if (running_ && !paused_ && mono >= deadline_) {
            if (!pomodoro_) {
                running_ = false;
                PushAlert(0, timer_title_, "countdown", TimeRingtone::Countdown);
            } else if (rest_ && round_ >= rounds_) {
                running_ = false;
                PushAlert(0, "Hoàn thành phiên Pomodoro!", "focus", TimeRingtone::PomodoroComplete);
            } else {
                if (rest_) {
                    ++round_;
                    rest_ = false;
                    total_ = focus_;
                } else {
                    rest_ = true;
                    total_ = round_ >= rounds_ ? long_break_ : short_break_;
                }
                deadline_ = mono + total_;
                PushAlert(0, rest_ ? "Đến giờ nghỉ ngơi" : "Bắt đầu tập trung",
                          rest_ ? "break" : "focus",
                          rest_ ? TimeRingtone::BreakStart : TimeRingtone::FocusStart, 6);
            }
        }
        while (!alerts_.empty() && alerts_.front().until != 0 && mono >= alerts_.front().until)
            alerts_.pop_front();
        if (!alerts_.empty() && alerts_.front().until == 0)
            alerts_.front().until = mono + alerts_.front().duration;
        view_ = {};
        if (!alerts_.empty()) {
            const auto& a = alerts_.front();
            view_.visible = true;
            view_.alert = true;
            view_.mode = a.mode;
            view_.title = a.title;
            view_.digits = LocalTime(now).substr(11, 5);
            view_.subtitle = "Chạm để xác nhận";
            view_.progress = (mono % 2) ? 1000 : 960;
        } else if (running_) {
            view_.visible = visible_;
            view_.paused = paused_;
            view_.mode = pomodoro_ ? (rest_ ? "break" : "focus") : "countdown";
            view_.title = timer_title_;
            int64_t remain = std::max<int64_t>(0, paused_ ? remaining_ : deadline_ - mono);
            char digits[24];
            if (remain >= 3600)
                snprintf(digits, sizeof(digits), "%02lld:%02lld:%02lld", remain / 3600,
                         remain / 60 % 60, remain % 60);
            else
                snprintf(digits, sizeof(digits), "%02lld:%02lld", remain / 60, remain % 60);
            view_.digits = digits;
            view_.progress = total_ ? static_cast<int>((total_ - remain) * 1000 / total_) : 0;
            view_.subtitle =
                paused_     ? "Đang tạm dừng"
                : pomodoro_ ? "Vòng " + std::to_string(round_) + " / " + std::to_string(rounds_)
                            : "Từng giây đều đáng giá";
        }
    }
    if (!pending_.exchange(true)) {
        Application::GetInstance().Schedule([weak = weak_from_this()] {
            if (auto self = weak.lock()) {
                TimeView view;
                TimeRingtone ringtone = TimeRingtone::None;
                bool interrupt_media = false;
                {
                    std::lock_guard lock(self->mutex_);
                    view = self->view_;
                    const auto state = Application::GetInstance().GetDeviceState();
                    if (!self->alerts_.empty() &&
                        self->presented_alert_ != self->alerts_.front().serial) {
                        self->presented_alert_ = self->alerts_.front().serial;
                        interrupt_media = true;
                    }
                    if (!self->alerts_.empty() && view.alert && Mono() >= self->next_sound_ &&
                        state == kDeviceStateIdle) {
                        ringtone = self->alerts_.front().ringtone;
                        self->next_sound_ = Mono() + 10;
                    }
                }
                if (!self->stopping_) {
                    auto& app = Application::GetInstance();
                    if (interrupt_media) {
                        if (app.GetDeviceState() == kDeviceStateRadio)
                            app.RequestStopRadio();
                        if (app.GetDeviceState() == kDeviceStateNotifying)
                            app.RequestStopMedia();
                    }
                    self->present_(view, ringtone);
                }
                self->pending_ = false;
            }
        });
    }
}
}  // namespace mrkenddo
