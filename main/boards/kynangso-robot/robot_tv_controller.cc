#include "robot_tv_controller.h"

#include "application.h"
#include "robot_live_tv.h"

#include <cJSON.h>
#include <cstring>

namespace kynangso {

bool RobotTvController::HandleCommand(const std::string& payload, std::string* result,
                                      std::string* error) {
    cJSON* root = cJSON_ParseWithLength(payload.data(), payload.size());
    auto text = [root](const char* key, size_t limit) -> const char* {
        auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
        return cJSON_IsString(item) && item->valuestring && item->valuestring[0] &&
                       std::strlen(item->valuestring) <= limit
                   ? item->valuestring
                   : nullptr;
    };
    const char* type = text("type", 48);
    bool accepted = false;
    if (type && std::strcmp(type, "tv.status") == 0) {
        accepted = true;
    } else if (type && std::strcmp(type, "tv.stop") == 0) {
        player_.Stop();
        accepted = true;
    } else if (type && std::strcmp(type, "tv.start") == 0) {
        const char* title = text("title", 128);
        const char* video = text("video_url", 767);
        const char* audio = text("audio_url", 767);
        const char* session = text("session_id", 48);
        const char* channel = text("channel_id", 48);
        if (title && video && audio && session && channel) {
            if (before_play_)
                before_play_();
            accepted = player_.Start(title, video, audio, error, session, channel);
        } else if (error)
            *error = "Thiếu tên kênh, ID phiên hoặc URL stream hợp lệ.";
    } else if (type && std::strcmp(type, "tv.display_benchmark") == 0) {
        accepted = player_.StartDisplayBenchmark(error);
    } else if (error) {
        *error = "Lệnh TV không được hỗ trợ.";
    }
    cJSON_Delete(root);
    if (accepted && result)
        *result = player_.StatusJson();
    return accepted;
}

bool RobotTvController::RequestService(const std::string& payload, std::string* result,
                                       std::string* error) {
    return Application::GetInstance().RequestService(payload, result, error);
}

std::string RobotTvController::StatusJson() {
    auto result = player_.StatusJson();
    cJSON* value = cJSON_Parse(result.c_str());
    cJSON_AddBoolToObject(value, "server_connected",
                          Application::GetInstance().IsDeviceBridgeConnected());
    char* encoded = cJSON_PrintUnformatted(value);
    result = encoded ? encoded : "{}";
    cJSON_free(encoded);
    cJSON_Delete(value);
    return result;
}

void RobotTvController::StopLocally() { player_.Stop(); }

}  // namespace kynangso
