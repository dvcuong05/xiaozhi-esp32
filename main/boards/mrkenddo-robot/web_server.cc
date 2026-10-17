#include "web_server.h"
#include "robot_controller.h"
#include "robot_drive.h"
#include "robot_settings.h"
#include "web_ui.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <esp_http_server.h>
#include <esp_log.h>
#include <application.h>
#include <cJSON.h>
#include <wifi_manager.h>

#define TAG "WEB_SERVER"

static const char* INDEX_HTML = mrkenddo::web_ui::kIndexHtml;

namespace {

constexpr size_t kMaxLogLineBytes = 384;
constexpr int kMaxPendingLogJobs = 24;

struct WebServerLogState {
    std::atomic<httpd_handle_t> server{nullptr};
    std::atomic<int> client_fd{-1};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> active{false};
};

std::atomic<int> g_pending_log_jobs{0};

}  // namespace

class WebServerLogStream {
public:
    WebServerLogStream() : state_(std::make_shared<WebServerLogState>()) {
        instance_.store(this);
    }

    ~WebServerLogStream() {
        Disable(-1);
        WebServerLogStream* expected = this;
        instance_.compare_exchange_strong(expected, nullptr);
    }

    void SetServer(httpd_handle_t server) { state_->server.store(server); }

    bool Enable(int client_fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_->server.load() == nullptr) {
            return false;
        }

        if (!state_->active.load()) {
            previous_vprintf_.store(esp_log_set_vprintf(LogVprintf));
        }
        state_->generation.fetch_add(1);
        state_->client_fd.store(client_fd);
        state_->active.store(true);
        return true;
    }

    void Disable(int client_fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_->active.load() ||
            (client_fd >= 0 && state_->client_fd.load() != client_fd)) {
            return;
        }

        state_->active.store(false);
        state_->client_fd.store(-1);
        state_->generation.fetch_add(1);
        vprintf_like_t previous = previous_vprintf_.exchange(nullptr);
        if (previous != nullptr) {
            esp_log_set_vprintf(previous);
        }
    }

    esp_err_t HandleWebSocket(httpd_req_t* req) {
        const int client_fd = httpd_req_to_sockfd(req);
        if (req->method == HTTP_GET) {
            // ESP-IDF 6.1 upgrades without invoking this handler. Capture starts
            // only after the client sends an explicit command on the open socket.
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "WebSocket upgrade required");
            return ESP_OK;
        }

        httpd_ws_frame_t frame = {};
        esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
        if (ret != ESP_OK) {
            return ret;
        }
        if (frame.len > 128) {
            Disable(client_fd);
            return ESP_ERR_INVALID_SIZE;
        }

        std::unique_ptr<uint8_t[]> payload;
        if (frame.len > 0) {
            payload = std::make_unique<uint8_t[]>(frame.len + 1);
            frame.payload = payload.get();
            ret = httpd_ws_recv_frame(req, &frame, frame.len);
            if (ret != ESP_OK) {
                return ret;
            }
        }

        if (frame.type == HTTPD_WS_TYPE_CLOSE) {
            Disable(client_fd);
            frame.len = 0;
            return httpd_ws_send_frame(req, &frame);
        }
        if (frame.type == HTTPD_WS_TYPE_PING) {
            frame.type = HTTPD_WS_TYPE_PONG;
            return httpd_ws_send_frame(req, &frame);
        }
        if (frame.type == HTTPD_WS_TYPE_TEXT && frame.len == 5 &&
            std::memcmp(frame.payload, "start", 5) == 0) {
            if (req->sess_ctx == nullptr) {
                auto* session = new (std::nothrow) LogSession{this, client_fd};
                if (session == nullptr) {
                    return ESP_ERR_NO_MEM;
                }
                req->sess_ctx = session;
                req->free_ctx = FreeSession;
            }
            if (!Enable(client_fd)) {
                return ESP_FAIL;
            }
            constexpr char kReady[] = "{\"type\":\"log_ready\"}";
            httpd_ws_frame_t ready = {};
            ready.type = HTTPD_WS_TYPE_TEXT;
            ready.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(kReady));
            ready.len = sizeof(kReady) - 1;
            ret = httpd_ws_send_frame(req, &ready);
            if (ret != ESP_OK) {
                Disable(client_fd);
                return ret;
            }
            ESP_LOGI(TAG, "Kenh log WebSocket da ket noi, fd=%d", client_fd);
        }
        return ESP_OK;
    }

private:
    struct LogSession {
        WebServerLogStream* stream;
        int client_fd;
    };

    static void FreeSession(void* ctx) {
        std::unique_ptr<LogSession> session(static_cast<LogSession*>(ctx));
        session->stream->Disable(session->client_fd);
    }

    struct LogJob {
        std::shared_ptr<WebServerLogState> state;
        int client_fd = -1;
        uint32_t generation = 0;
        size_t length = 0;
        char payload[kMaxLogLineBytes] = {};
    };

    static int LogVprintf(const char* format, va_list args) {
        WebServerLogStream* stream = instance_.load();
        if (stream == nullptr) {
            return vprintf(format, args);
        }

        const auto state = stream->state_;
        char line[kMaxLogLineBytes];
        va_list line_args;
        va_copy(line_args, args);
        const int written = vsnprintf(line, sizeof(line), format, line_args);
        va_end(line_args);

        int result = 0;
        vprintf_like_t previous = stream->previous_vprintf_.load();
        if (previous != nullptr) {
            va_list previous_args;
            va_copy(previous_args, args);
            result = previous(format, previous_args);
            va_end(previous_args);
        } else {
            va_list fallback_args;
            va_copy(fallback_args, args);
            result = vprintf(format, fallback_args);
            va_end(fallback_args);
        }

        if (written <= 0 || !state->active.load()) {
            return result;
        }

        stream->Queue(state, line, static_cast<size_t>(written));
        return result;
    }

    void Queue(const std::shared_ptr<WebServerLogState>& state, const char* line,
               size_t length) {
        int pending = g_pending_log_jobs.load();
        while (pending < kMaxPendingLogJobs &&
               !g_pending_log_jobs.compare_exchange_weak(pending, pending + 1)) {
        }
        if (pending >= kMaxPendingLogJobs) {
            return;
        }

        auto* job = new (std::nothrow) LogJob();
        if (job == nullptr) {
            g_pending_log_jobs.fetch_sub(1);
            return;
        }
        job->state = state;
        job->client_fd = state->client_fd.load();
        job->generation = state->generation.load();
        job->length = std::min(length, kMaxLogLineBytes - 1);
        std::memcpy(job->payload, line, job->length);

        httpd_handle_t server = state->server.load();
        if (server == nullptr || httpd_queue_work(server, SendJob, job) != ESP_OK) {
            delete job;
            g_pending_log_jobs.fetch_sub(1);
        }
    }

    static void SendJob(void* arg) {
        std::unique_ptr<LogJob> job(static_cast<LogJob*>(arg));
        const auto& state = job->state;
        httpd_handle_t server = state->server.load();
        bool sent = false;
        bool current_job = server != nullptr && state->active.load() &&
                           state->client_fd.load() == job->client_fd &&
                           state->generation.load() == job->generation;
        httpd_ws_client_info_t client_info = HTTPD_WS_CLIENT_INVALID;
        if (current_job) {
            client_info = httpd_ws_get_fd_info(server, job->client_fd);
            if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_frame_t frame = {};
                frame.type = HTTPD_WS_TYPE_TEXT;
                frame.final = true;
                frame.payload = reinterpret_cast<uint8_t*>(job->payload);
                frame.len = job->length;
                sent = httpd_ws_send_frame_async(server, job->client_fd, &frame) == ESP_OK;
            }
        }
        if (!sent && current_job) {
            WebServerLogStream* stream = instance_.load();
            if (stream != nullptr) {
                stream->Disable(job->client_fd);
            }
        }
        g_pending_log_jobs.fetch_sub(1);
    }

    std::shared_ptr<WebServerLogState> state_;
    std::atomic<vprintf_like_t> previous_vprintf_{nullptr};
    std::mutex mutex_;
    static std::atomic<WebServerLogStream*> instance_;
};

std::atomic<WebServerLogStream*> WebServerLogStream::instance_{nullptr};

namespace {

constexpr size_t kMaxJsonBodyBytes = 8192;
constexpr int kMinOledAddress = 0x03;
constexpr int kMaxOledAddress = 0x77;

esp_err_t ReceiveBody(httpd_req_t* req, std::string* body) {
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body is required");
        return ESP_ERR_INVALID_SIZE;
    }
    if (req->content_len > kMaxJsonBodyBytes) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request body too large");
        return ESP_ERR_INVALID_SIZE;
    }

    body->assign(req->content_len, '\0');
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body->data() + received, req->content_len - received);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Khong the nhan du body HTTP (%d)", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Failed to receive complete request body");
            return ESP_FAIL;
        }
        received += static_cast<size_t>(ret);
    }
    return ESP_OK;
}

cJSON* ParseBody(httpd_req_t* req) {
    std::string body;
    if (ReceiveBody(req, &body) != ESP_OK) {
        return nullptr;
    }
    cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    return root;
}

esp_err_t SendJson(httpd_req_t* req, cJSON* response) {
    if (response == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_FAIL;
    }
    char* response_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (response_str == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize response");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, response_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(response_str);
    return ret;
}

bool GetInteger(const cJSON* object, const char* name, int min_value, int max_value, int* value,
                bool required = true) {
    const cJSON* item = cJSON_GetObjectItem(object, name);
    if (item == nullptr) {
        return !required;
    }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble || item->valuedouble < min_value ||
        item->valuedouble > max_value) {
        return false;
    }
    *value = static_cast<int>(item->valuedouble);
    return true;
}

bool GetString(const cJSON* object, const char* name, char* value, size_t value_size,
               bool required = true) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) {
        return !required;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr ||
        std::strlen(item->valuestring) >= value_size) {
        return false;
    }
    std::snprintf(value, value_size, "%s", item->valuestring);
    return true;
}

bool ParseMediaMetadata(const cJSON* root, MediaPlaybackMetadata* metadata) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsObject(item)) {
        return false;
    }
    char title[193] = {};
    char artist[129] = {};
    char album[129] = {};
    char station[129] = {};
    if (!GetString(item, "title", title, sizeof(title), false) ||
        !GetString(item, "artist", artist, sizeof(artist), false) ||
        !GetString(item, "album", album, sizeof(album), false) ||
        !GetString(item, "station", station, sizeof(station), false)) {
        return false;
    }
    metadata->title = title;
    metadata->artist = artist;
    metadata->album = album;
    metadata->station = station;
    const cJSON* duration = cJSON_GetObjectItemCaseSensitive(item, "duration_ms");
    if (duration != nullptr) {
        if (!cJSON_IsNumber(duration) || !std::isfinite(duration->valuedouble) ||
            std::floor(duration->valuedouble) != duration->valuedouble ||
            duration->valuedouble < 0 ||
            duration->valuedouble > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        metadata->duration_ms = static_cast<uint32_t>(duration->valuedouble);
    }
    return true;
}

bool ParseSubtitles(const cJSON* root, std::vector<NotifySubtitle>* subtitles) {
    const cJSON* list = cJSON_GetObjectItemCaseSensitive(root, "subtitles");
    if (list == nullptr) {
        return true;
    }
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > 48) {
        return false;
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach (item, list) {
        const cJSON* start = cJSON_GetObjectItemCaseSensitive(item, "start_ms");
        char text[97] = {};
        if (!cJSON_IsObject(item) || !cJSON_IsNumber(start) || !std::isfinite(start->valuedouble) ||
            std::floor(start->valuedouble) != start->valuedouble || start->valuedouble < 0 ||
            start->valuedouble > std::numeric_limits<uint32_t>::max() ||
            !GetString(item, "text", text, sizeof(text))) {
            return false;
        }
        subtitles->push_back({static_cast<uint32_t>(start->valuedouble), std::string(text)});
    }
    return true;
}

bool ParseOledAddress(const cJSON* item, int* address) {
    if (item == nullptr || !cJSON_IsString(item) || item->valuestring == nullptr ||
        item->valuestring[0] == '\0' || std::strlen(item->valuestring) > 2) {
        return false;
    }
    char* end = nullptr;
    long parsed = std::strtol(item->valuestring, &end, 16);
    if (end == item->valuestring || *end != '\0' || parsed < kMinOledAddress ||
        parsed > kMaxOledAddress) {
        return false;
    }
    *address = static_cast<int>(parsed);
    return true;
}

WebServer* GetServer(httpd_req_t* req) { return static_cast<WebServer*>(req->user_ctx); }

}  // namespace

WebServer::WebServer(mrkenddo::RobotController& controller)
    : server_(nullptr), controller_(controller), log_stream_(std::make_unique<WebServerLogStream>()) {}

WebServer::~WebServer() { Stop(); }

bool WebServer::Start(int port) {
    if (port < 1 || port > 65535 || server_ != nullptr) {
        ESP_LOGE(TAG, "Cong HTTP khong hop le hoac server da chay: %d", port);
        return false;
    }

    httpd_uri_t uris[] = {
        {"/", HTTP_GET, RootHandler, this},
        {"/api/logs", HTTP_GET, LogWebSocketHandler, this, true, true},
        {"/api/notify", HTTP_POST, ApiNotifyHandler, this},
        {"/api/notify/stop", HTTP_POST, ApiNotifyStopHandler, this},
        {"/api/media/play", HTTP_POST, ApiMediaPlayHandler, this},
        {"/api/media/stop", HTTP_POST, ApiMediaStopHandler, this},
        {"/api/radio/start", HTTP_POST, ApiRadioStartHandler, this},
        {"/api/radio/stop", HTTP_POST, ApiRadioStopHandler, this},
        {"/api/radio/status", HTTP_GET, ApiRadioStatusHandler, this},
        {"/api/prompt", HTTP_POST, ApiPromptHandler, this},
        {"/api/prompt_status", HTTP_GET, ApiPromptStatusHandler, this},
        {"/api/move", HTTP_POST, ApiMoveHandler, this},
        {"/api/stop", HTTP_POST, ApiStopHandler, this},
        {"/api/routine", HTTP_POST, ApiRoutineHandler, this},
        {"/api/motion_status", HTTP_GET, ApiMotionStatusHandler, this},
        {"/api/config", HTTP_POST, ApiConfigHandler, this},
        {"/api/config", HTTP_GET, ApiConfigHandler, this},
        {"/api/oled_config", HTTP_POST, ApiOledConfigHandler, this},
        {"/api/oled_config", HTTP_GET, ApiOledConfigHandler, this},
        {"/api/oled_test", HTTP_POST, ApiOledTestHandler, this},
        {"/api/oled_message", HTTP_POST, ApiOledMessageHandler, this},
        {"/api/oled_clear", HTTP_POST, ApiOledClearHandler, this},
        {"/api/status", HTTP_GET, ApiStatusHandler, this},
        {"/api/tft_config", HTTP_GET, ApiTftConfigHandler, this},
        {"/api/tft_config", HTTP_POST, ApiTftConfigHandler, this},
        {"/api/boot_config", HTTP_GET, ApiBootConfigHandler, this},
        {"/api/boot_config", HTTP_POST, ApiBootConfigHandler, this},
        {"/api/qr_config", HTTP_GET, ApiQrConfigHandler, this},
        {"/api/qr_config", HTTP_POST, ApiQrConfigHandler, this},
        {"/api/qr/show", HTTP_POST, ApiQrShowHandler, this},
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;
    config.max_uri_handlers = sizeof(uris) / sizeof(uris[0]);

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong the khoi dong HTTP server: %s", esp_err_to_name(ret));
        return false;
    }
    log_stream_->SetServer(server_);

    for (auto& uri : uris) {
        ret = httpd_register_uri_handler(server_, &uri);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the dang ky URI %s: %s", uri.uri, esp_err_to_name(ret));
            Stop();
            return false;
        }
    }

    ESP_LOGW(TAG, "HTTP server cong %d khong co xac thuc; chi nen dung trong mang tin cay", port);
    ESP_LOGI(TAG, "HTTP server da khoi dong tren cong %d", port);
    return true;
}

void WebServer::Stop() {
    if (server_) {
        log_stream_->Disable(-1);
        httpd_stop(server_);
        log_stream_->SetServer(nullptr);
        server_ = nullptr;
        ESP_LOGI(TAG, "HTTP server da dung");
    }
}

esp_err_t WebServer::RootHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::LogWebSocketHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->log_stream_ == nullptr) {
        return ESP_FAIL;
    }
    return server->log_stream_->HandleWebSocket(req);
}

esp_err_t WebServer::ApiQrConfigHandler(httpd_req_t* req) {
    auto& controller = static_cast<WebServer*>(req->user_ctx)->controller_;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (req->method == HTTP_GET) {
        const auto config = controller.GetQrConfig();
        auto* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "payload", config.payload);
        cJSON_AddStringToObject(json, "caption", config.caption);
        cJSON_AddBoolToObject(json, "valid", config.valid);
        cJSON_AddStringToObject(json, "source", config.payload[0] ? "custom" : "default_bitmap");
        return SendJson(req, json);
    }
    auto* root = ParseBody(req);
    if (root == nullptr)
        return ESP_FAIL;
    mrkenddo::QrConfig config;
    const bool valid = GetString(root, "payload", config.payload, sizeof(config.payload)) &&
                       GetString(root, "caption", config.caption, sizeof(config.caption), false) &&
                       mrkenddo::RobotQrCode::ValidateConfig(config);
    cJSON_Delete(root);
    if (!valid) {
        return httpd_resp_send_err(
            req, HTTPD_400_BAD_REQUEST,
            "QR payload max 256 UTF-8 bytes; caption max 96; no control characters");
    }
    if (!controller.ConfigureQr(config)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot save QR config");
    }
    auto* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "result",
                            "QR config saved; scan and verify recipient before use");
    return SendJson(req, json);
}

esp_err_t WebServer::ApiQrShowHandler(httpd_req_t* req) {
    auto* root = ParseBody(req);
    if (root == nullptr)
        return ESP_FAIL;
    char type[12] = "payment";
    mrkenddo::QrConfig config;
    int duration = 30000;
    const bool valid = GetString(root, "type", type, sizeof(type), false) &&
                       GetInteger(root, "duration_ms", 5000, 120000, &duration, false) &&
                       GetString(root, "payload", config.payload, sizeof(config.payload), false) &&
                       GetString(root, "caption", config.caption, sizeof(config.caption), false) &&
                       mrkenddo::RobotQrCode::ValidateConfig(config);
    cJSON_Delete(root);
    if (!valid)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid QR request");
    auto& controller = static_cast<WebServer*>(req->user_ctx)->controller_;
    bool scheduled = false;
    if (std::strcmp(type, "payment") == 0)
        scheduled = controller.ShowPaymentQr(duration);
    else if (std::strcmp(type, "ip") == 0)
        scheduled = controller.ShowIpQr(duration);
    else if (std::strcmp(type, "custom") == 0)
        scheduled = controller.ShowQr(config.payload, config.caption, duration);
    else
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown QR type");
    if (!scheduled) {
        httpd_resp_set_status(req, "409 Conflict");
        auto* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error",
                                "Cannot show QR; check payload, stored config and Wi-Fi");
        return SendJson(req, json);
    }
    auto* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "result", "QR display scheduled");
    return SendJson(req, json);
}

esp_err_t WebServer::ApiNotifyHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }

    char audio_url[768] = {};
    bool valid = GetString(root, "audio_url", audio_url, sizeof(audio_url));
    valid = valid && (std::strncmp(audio_url, "http://", 7) == 0 ||
                      std::strncmp(audio_url, "https://", 8) == 0);

    std::vector<NotifySubtitle> subtitles;
    const cJSON* subtitles_json = cJSON_GetObjectItemCaseSensitive(root, "subtitles");
    if (valid && subtitles_json != nullptr) {
        if (!cJSON_IsArray(subtitles_json) || cJSON_GetArraySize(subtitles_json) > 32) {
            valid = false;
        } else {
            const cJSON* item = nullptr;
            cJSON_ArrayForEach (item, subtitles_json) {
                int start_ms = 0;
                char text[256] = {};
                if (!cJSON_IsObject(item) ||
                    !GetInteger(item, "start_ms", 0, std::numeric_limits<int>::max(), &start_ms) ||
                    !GetString(item, "text", text, sizeof(text))) {
                    valid = false;
                    break;
                }
                subtitles.push_back(
                    NotifySubtitle{static_cast<uint32_t>(start_ms), std::string(text)});
            }
        }
    }

    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid notify payload");
        return ESP_FAIL;
    }

    Application::GetInstance().RequestNotification(std::string(audio_url), std::move(subtitles));
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiNotifyStopHandler(httpd_req_t* req) {
    Application::GetInstance().RequestStopNotification();
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "stopping");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMediaPlayHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    char audio_url[768] = {};
    bool valid =
        GetString(root, "audio_url", audio_url, sizeof(audio_url)) &&
        (std::strncmp(audio_url, "http://", 7) == 0 || std::strncmp(audio_url, "https://", 8) == 0);
    MediaPlaybackMetadata metadata;
    std::vector<NotifySubtitle> subtitles;
    valid = valid && ParseMediaMetadata(root, &metadata) && ParseSubtitles(root, &subtitles);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid media payload");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Media request title=%s artist=%s timed_lyrics=%u", metadata.title.c_str(),
             metadata.artist.c_str(), static_cast<unsigned>(subtitles.size()));
    Application::GetInstance().RequestMedia(std::string(audio_url), std::move(metadata),
                                            std::move(subtitles));
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMediaStopHandler(httpd_req_t* req) {
    Application::GetInstance().RequestStopMedia();
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "stopping");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStartHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }

    char audio_url[768] = {};
    bool valid = GetString(root, "audio_url", audio_url, sizeof(audio_url));
    valid = valid && (std::strncmp(audio_url, "http://", 7) == 0 ||
                      std::strncmp(audio_url, "https://", 8) == 0);
    MediaPlaybackMetadata metadata;
    valid = valid && ParseMediaMetadata(root, &metadata);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid radio payload");
        return ESP_FAIL;
    }

    Application::GetInstance().RequestRadio(std::string(audio_url), std::move(metadata));
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStopHandler(httpd_req_t* req) {
    Application::GetInstance().RequestStopRadio();
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "stopping");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStatusHandler(httpd_req_t* req) {
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        const bool active = Application::GetInstance().IsRadioActive();
        cJSON_AddBoolToObject(response, "active", active);
        cJSON_AddStringToObject(response, "state", active ? "radio" : "idle");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiPromptHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    const cJSON* prompt = cJSON_GetObjectItem(root, "prompt");
    if (!cJSON_IsString(prompt) || prompt->valuestring == nullptr ||
        prompt->valuestring[0] == '\0' || std::strlen(prompt->valuestring) > 1000) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid prompt");
        return ESP_FAIL;
    }
    bool queued = GetServer(req)->controller_.QueueWebPrompt(prompt->valuestring);
    cJSON_Delete(root);
    if (!queued) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "A prompt is already pending or the robot is busy",
                               HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiPromptStatusHandler(httpd_req_t* req) {
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", GetServer(req)->controller_.WebPromptStatus());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMoveHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    int steps = 0;
    int delay_ms = 0;
    bool valid = GetInteger(root, "steps", -RobotDrive::kMaxSteps, RobotDrive::kMaxSteps, &steps) &&
                 GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                            &delay_ms, false);
    // Keep the historical optional delay_ms API, but reject a malformed value
    // when the field is present.
    if (valid && cJSON_GetObjectItem(root, "delay_ms") != nullptr) {
        valid = GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                           &delay_ms);
    }
    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    const cJSON* direction_item = cJSON_GetObjectItemCaseSensitive(root, "direction");
    RobotDrive::Direction direction =
        steps < 0 ? RobotDrive::Direction::Backward : RobotDrive::Direction::Forward;
    if (direction_item != nullptr) {
        if (!cJSON_IsString(direction_item) || direction_item->valuestring == nullptr ||
            steps <= 0) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction or steps");
            return ESP_FAIL;
        }
        const char* value = direction_item->valuestring;
        if (std::strcmp(value, "forward") == 0)
            direction = RobotDrive::Direction::Forward;
        else if (std::strcmp(value, "backward") == 0)
            direction = RobotDrive::Direction::Backward;
        else if (std::strcmp(value, "left") == 0)
            direction = RobotDrive::Direction::Left;
        else if (std::strcmp(value, "right") == 0)
            direction = RobotDrive::Direction::Right;
        else {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction");
            return ESP_FAIL;
        }
    }
    bool started = direction_item == nullptr ? server->controller_.Step(steps, delay_ms)
                                             : server->controller_.Move(direction, steps, delay_ms);
    cJSON_Delete(root);
    if (!started) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Motor command failed");
        return ESP_FAIL;
    }
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Moving");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiStopHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    server->controller_.Stop();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Stopped");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRoutineHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    const cJSON* routine = cJSON_GetObjectItemCaseSensitive(root, "routine");
    if (!cJSON_IsString(routine) || routine->valuestring == nullptr) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid routine");
        return ESP_FAIL;
    }

    RobotDrive::Routine routine_type;
    if (std::strcmp(routine->valuestring, "lively") == 0) {
        routine_type = RobotDrive::Routine::Lively;
    } else if (std::strcmp(routine->valuestring, "dance") == 0) {
        routine_type = RobotDrive::Routine::Dance;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Routine must be lively or dance");
        return ESP_FAIL;
    }

    const bool started = server->controller_.StartRoutine(routine_type);
    cJSON_Delete(root);
    if (!started) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Robot is already moving", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Routine started");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMotionStatusHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(response, "routine_running",
                              server != nullptr && server->controller_.IsRoutineRunning());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiConfigHandler(httpd_req_t* req) {
    auto& controller = GetServer(req)->controller_;
    auto config = controller.GetMotorConfig();
    if (req->method == HTTP_GET) {
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr) {
            cJSON_AddNumberToObject(response, "steps", config.steps);
            cJSON_AddNumberToObject(response, "delay_ms", config.delay_ms);
            cJSON_AddBoolToObject(response, "left_inverted", config.left_inverted);
            cJSON_AddBoolToObject(response, "right_inverted", config.right_inverted);
        }
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    int steps = 0;
    int delay_ms = 0;
    if (!GetInteger(root, "steps", 1, RobotDrive::kMaxSteps, &steps) ||
        !GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                    &delay_ms)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    const cJSON* left_item = cJSON_GetObjectItemCaseSensitive(root, "left_inverted");
    const cJSON* right_item = cJSON_GetObjectItemCaseSensitive(root, "right_inverted");
    if ((left_item != nullptr && !cJSON_IsBool(left_item)) ||
        (right_item != nullptr && !cJSON_IsBool(right_item))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid motor inversion");
        return ESP_FAIL;
    }
    config.steps = steps;
    config.delay_ms = delay_ms;
    // Older clients may omit polarity flags. Keep the saved polarity in that case.
    if (left_item != nullptr)
        config.left_inverted = cJSON_IsTrue(left_item);
    if (right_item != nullptr)
        config.right_inverted = cJSON_IsTrue(right_item);
    cJSON_Delete(root);
    if (!controller.ConfigureMotor(config)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Configuration saved");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledConfigHandler(httpd_req_t* req) {
    auto& controller = GetServer(req)->controller_;
    if (req->method == HTTP_GET) {
        const auto config = controller.GetOledConfig();
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr) {
            cJSON_AddStringToObject(response, "addr", config.address);
            cJSON_AddNumberToObject(response, "sda", config.sda);
            cJSON_AddNumberToObject(response, "scl", config.scl);
            cJSON_AddNumberToObject(response, "width", config.width);
            cJSON_AddNumberToObject(response, "height", config.height);
            cJSON_AddNumberToObject(response, "rotation", config.rotation);
            cJSON_AddNumberToObject(response, "mode", config.mode);
            cJSON_AddStringToObject(response, "text", config.text);
            cJSON_AddNumberToObject(response, "speed", config.speed);
            cJSON_AddNumberToObject(response, "font_size", config.font_size);
            cJSON_AddNumberToObject(response, "contrast", config.contrast);
            cJSON_AddBoolToObject(response, "invert", config.invert);
        }
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    // Optional fields retain the defaults declared by the shared settings schema.
    mrkenddo::OledConfig config{};
    int address = 0;
    int sda = 0;
    int scl = 0;
    int width = 0;
    int height = 0;
    int rotation = config.rotation;
    int mode = config.mode;
    int speed = config.speed;
    int font_size = config.font_size;
    int contrast = config.contrast;
    bool valid = ParseOledAddress(cJSON_GetObjectItem(root, "addr"), &address) &&
                 GetInteger(root, "sda", 0, std::numeric_limits<int>::max(), &sda) &&
                 GetInteger(root, "scl", 0, std::numeric_limits<int>::max(), &scl) &&
                 GetInteger(root, "width", 64, 256, &width) &&
                 GetInteger(root, "height", 16, 64, &height) &&
                 GetInteger(root, "rotation", 0, 180, &rotation, false) &&
                 GetInteger(root, "mode", 0, 3, &mode, false) &&
                 GetString(root, "text", config.text, sizeof(config.text), false) &&
                 GetInteger(root, "speed", 1, 5, &speed, false) &&
                 GetInteger(root, "font_size", 0, 2, &font_size, false) &&
                 GetInteger(root, "contrast", 0, 255, &contrast, false);
    const cJSON* invert_item = cJSON_GetObjectItemCaseSensitive(root, "invert");
    if (invert_item != nullptr) {
        valid = valid && cJSON_IsBool(invert_item);
        config.invert = cJSON_IsTrue(invert_item);
    }
    cJSON_Delete(root);
    std::snprintf(config.address, sizeof(config.address), "%02X", static_cast<unsigned>(address));
    config.sda = sda;
    config.scl = scl;
    config.width = width;
    config.height = height;
    config.rotation = rotation;
    config.mode = mode;
    config.speed = speed;
    config.font_size = font_size;
    config.contrast = contrast;
    if (!valid || !mrkenddo::RobotSettings::ValidOledPins(sda, scl) ||
        !mrkenddo::RobotSettings::ValidateOled(config)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OLED configuration");
        return ESP_FAIL;
    }
    if (!controller.ConfigureOled(config)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "OLED configuration saved and applying");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledTestHandler(httpd_req_t* req) {
    GetServer(req)->controller_.ReinitializeOled();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Display reinitialization scheduled");
        cJSON_AddStringToObject(response, "note", "Check if the clock is visible on display");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledMessageHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    char text[sizeof(mrkenddo::OledConfig::text)] = {};
    const mrkenddo::OledConfig defaults{};
    int speed = defaults.speed;
    int font_size = defaults.font_size;
    bool valid = GetString(root, "text", text, sizeof(text)) && text[0] != '\0' &&
                 GetInteger(root, "speed", 1, 5, &speed, false) &&
                 GetInteger(root, "font_size", 0, 2, &font_size, false);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OLED message");
        return ESP_FAIL;
    }
    if (!GetServer(req)->controller_.SaveOledMessage(text, speed, font_size)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save OLED message");
        return ESP_FAIL;
    }
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "OLED marquee scheduled");
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledClearHandler(httpd_req_t* req) {
    if (!GetServer(req)->controller_.SetOledMode(3)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to clear OLED");
        return ESP_FAIL;
    }
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "OLED cleared");
    return SendJson(req, response);
}

const char* DeviceStateName(DeviceState state) {
    switch (state) {
        case kDeviceStateStarting:
            return "starting";
        case kDeviceStateWifiConfiguring:
            return "wifi_configuring";
        case kDeviceStateIdle:
            return "idle";
        case kDeviceStateConnecting:
            return "connecting";
        case kDeviceStateListening:
            return "listening";
        case kDeviceStateSpeaking:
            return "speaking";
        case kDeviceStateNotifying:
            return "notifying";
        case kDeviceStateRadio:
            return "radio";
        case kDeviceStateUpgrading:
            return "upgrading";
        case kDeviceStateActivating:
            return "activating";
        case kDeviceStateAudioTesting:
            return "audio_testing";
        case kDeviceStateFatalError:
            return "fatal_error";
        case kDeviceStateUnknown:
        default:
            return "unknown";
    }
}

esp_err_t WebServer::ApiStatusHandler(httpd_req_t* req) {
    const auto& wifi = WifiManager::GetInstance();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(response, "wifi_connected", wifi.IsConnected());
        cJSON_AddStringToObject(response, "ip",
                                wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
        cJSON_AddStringToObject(response, "state",
                                DeviceStateName(Application::GetInstance().GetDeviceState()));
        cJSON_AddNumberToObject(response, "free_heap", esp_get_free_heap_size());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiTftConfigHandler(httpd_req_t* req) {
    auto& controller = GetServer(req)->controller_;
    if (req->method == HTTP_GET) {
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr)
            cJSON_AddNumberToObject(response, "rotation", controller.GetTftRotation());
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr)
        return ESP_FAIL;
    int rotation = 0;
    bool valid = GetInteger(root, "rotation", 0, 270, &rotation) &&
                 mrkenddo::RobotSettings::IsValidTftRotation(rotation);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid TFT rotation");
        return ESP_FAIL;
    }
    if (!controller.SetTftRotation(rotation)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "TFT rotation scheduled");
    return SendJson(req, response);
}

esp_err_t WebServer::ApiBootConfigHandler(httpd_req_t* req) {
    auto& controller = GetServer(req)->controller_;
    auto config = controller.GetBootAudioConfig();
    if (req->method == HTTP_POST) {
        cJSON* root = ParseBody(req);
        if (root == nullptr) {
            return ESP_FAIL;
        }

        const cJSON* enabled_json = cJSON_GetObjectItem(root, "enabled");
        int volume = config.volume;
        bool valid = cJSON_IsBool(enabled_json) && GetInteger(root, "volume", 0, 100, &volume);
        if (valid) {
            config.enabled = cJSON_IsTrue(enabled_json);
            config.volume = volume;
        }
        cJSON_Delete(root);
        if (!valid) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid boot audio configuration");
            return ESP_FAIL;
        }
        if (!controller.SetBootAudioConfig(config)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Failed to save boot audio configuration");
            return ESP_FAIL;
        }
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(response, "enabled", config.enabled);
        cJSON_AddNumberToObject(response, "volume", config.volume);
        if (req->method == HTTP_POST) {
            cJSON_AddStringToObject(response, "result", "Boot sequence configuration saved");
        }
    }
    return SendJson(req, response);
}
