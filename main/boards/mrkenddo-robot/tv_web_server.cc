#include "tv_web_server.h"

#include "robot_tv_controller.h"
#include "tv_web_ui.h"

#include <esp_heap_caps.h>
#include <cJSON.h>
#include <freertos/idf_additions.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace mrkenddo {
namespace {
esp_err_t Reply(httpd_req_t* req, const std::string& json) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.data(), json.size());
}

esp_err_t Error(httpd_req_t* req, const char* message) {
    cJSON* value = cJSON_CreateObject();
    cJSON_AddBoolToObject(value, "ok", false);
    cJSON_AddStringToObject(value, "error", message);
    char* encoded = cJSON_PrintUnformatted(value);
    std::string json = encoded ? encoded : "{\"ok\":false}";
    cJSON_free(encoded);
    cJSON_Delete(value);
    return Reply(req, json);
}
}  // namespace

struct TvWebServer::Job {
    TvWebServer* owner;
    httpd_req_t* request;
};

TvWebServer::TvWebServer(RobotTvController& controller) : controller_(controller) {
    idle_ = xSemaphoreCreateBinary();
}

TvWebServer::~TvWebServer() {
    Stop();
    if (idle_)
        vSemaphoreDelete(idle_);
}

void TvWebServer::Stop() {
    bool wait;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true;
        wait = busy_;
    }
    if (wait && idle_)
        xSemaphoreTake(idle_, portMAX_DELAY);
}

esp_err_t TvWebServer::Register(httpd_handle_t server) {
    closing_ = false;
    httpd_uri_t routes[] = {
        {"/tv", HTTP_GET, Page, this},
        {"/api/tv/status", HTTP_GET, Status, this},
        {"/api/tv/stop", HTTP_POST, StopPlayback, this},
        {"/api/tv/channels", HTTP_GET, Handle, this},
        {"/api/tv/channel", HTTP_GET, Handle, this},
        {"/api/tv/channel", HTTP_POST, Handle, this},
        {"/api/tv/channel/delete", HTTP_POST, Handle, this},
        {"/api/tv/check", HTTP_POST, Handle, this},
        {"/api/tv/play", HTTP_POST, Handle, this},
    };
    static_assert(sizeof(routes) / sizeof(routes[0]) == kHandlerCount);
    for (auto& route : routes) {
        auto error = httpd_register_uri_handler(server, &route);
        if (error != ESP_OK)
            return error;
    }
    return ESP_OK;
}

esp_err_t TvWebServer::Page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kRobotTvPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t TvWebServer::Status(httpd_req_t* req) {
    return Reply(req, static_cast<TvWebServer*>(req->user_ctx)->controller_.StatusJson());
}

esp_err_t TvWebServer::StopPlayback(httpd_req_t* req) {
    // Local stop never waits behind a catalog request or a disconnected VPS.
    static_cast<TvWebServer*>(req->user_ctx)->controller_.StopLocally();
    if (req->content_len > 128) {
        httpd_resp_set_hdr(req, "Connection", "close");
        return Error(req, "Đã dừng TV; dữ liệu yêu cầu quá lớn.");
    }
    char discard[128];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, discard, remaining);
        if (received <= 0)
            return ESP_FAIL;
        remaining -= received;
    }
    return Reply(req, "{\"ok\":true,\"state\":\"stopped\"}");
}

esp_err_t TvWebServer::Handle(httpd_req_t* req) {
    auto& self = *static_cast<TvWebServer*>(req->user_ctx);
    std::lock_guard<std::mutex> lock(self.mutex_);
    if (self.closing_ || self.busy_ || !self.idle_ || req->content_len > 4096) {
        if (req->content_len)
            httpd_resp_set_hdr(req, "Connection", "close");
        return Error(req, "TV đang bận hoặc yêu cầu quá lớn. Vui lòng thử lại.");
    }
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK)
        return ESP_FAIL;
    auto* job = new (std::nothrow) Job{&self, copy};
    self.busy_ = true;
    xSemaphoreTake(self.idle_, 0);
    if (!job || xTaskCreateWithCaps(RunJob, "tv_http", 8192, job, 2, nullptr,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        delete job;
        self.busy_ = false;
        auto handle = copy->handle;
        int socket = httpd_req_to_sockfd(copy);
        Error(copy, "Không đủ bộ nhớ cho yêu cầu TV.");
        httpd_req_async_handler_complete(copy);
        httpd_sess_trigger_close(handle, socket);
    }
    return ESP_OK;
}

void TvWebServer::RunJob(void* context) {
    std::unique_ptr<Job> job(static_cast<Job*>(context));
    auto& self = *job->owner;
    auto handle = job->request->handle;
    int socket = httpd_req_to_sockfd(job->request);
    auto result = self.Request(job->request);
    httpd_req_async_handler_complete(job->request);
    if (result != ESP_OK)
        httpd_sess_trigger_close(handle, socket);
    {
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.busy_ = false;
        xSemaphoreGive(self.idle_);
    }
    job.reset();
    vTaskDeleteWithCaps(nullptr);
}

esp_err_t TvWebServer::Request(httpd_req_t* req) {
    cJSON* command = nullptr;
    if (req->method == HTTP_POST) {
        std::string body(req->content_len, '\0');
        size_t copied = 0;
        while (copied < body.size()) {
            int received = httpd_req_recv(req, body.data() + copied, body.size() - copied);
            if (received <= 0) {
                Error(req, "Không đọc đủ dữ liệu yêu cầu.");
                return ESP_FAIL;
            }
            copied += received;
        }
        command = cJSON_ParseWithLength(body.data(), body.size());
    } else {
        command = cJSON_CreateObject();
        size_t length = httpd_req_get_url_query_len(req);
        if (length > 640) {
            cJSON_Delete(command);
            return Error(req, "Từ khóa hoặc ID quá dài.");
        }
        if (length) {
            std::string query(length + 1, '\0');
            httpd_req_get_url_query_str(req, query.data(), query.size());
            // esp_http_server returns percent-encoded query values. Decode
            // bytes without form-decoding the full query (escaped & stays data).
            for (const char* key : {"query", "page", "id"}) {
                char raw[512];
                if (httpd_query_key_value(query.c_str(), key, raw, sizeof(raw)) == ESP_OK) {
                    std::string decoded;
                    auto hex = [](char c) -> int {
                        if (c >= '0' && c <= '9')
                            return c - '0';
                        if (c >= 'a' && c <= 'f')
                            return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F')
                            return c - 'A' + 10;
                        return -1;
                    };
                    for (size_t i = 0; raw[i]; ++i) {
                        if (raw[i] == '%' && raw[i + 1] && raw[i + 2] && hex(raw[i + 1]) >= 0 &&
                            hex(raw[i + 2]) >= 0) {
                            char byte = (hex(raw[i + 1]) << 4) | hex(raw[i + 2]);
                            if (byte == 0) {
                                cJSON_Delete(command);
                                return Error(req, "Query không hợp lệ.");
                            }
                            decoded += byte;
                            i += 2;
                        } else
                            decoded += raw[i] == '+' ? ' ' : raw[i];
                    }
                    if (std::strcmp(key, "page") == 0) {
                        char* end = nullptr;
                        long page = std::strtol(decoded.c_str(), &end, 10);
                        if (!end || end == decoded.c_str() || *end || page < 0 || page > 99) {
                            cJSON_Delete(command);
                            return Error(req, "Trang không hợp lệ.");
                        }
                        cJSON_AddNumberToObject(command, key, page);
                    } else
                        cJSON_AddStringToObject(command, key, decoded.c_str());
                }
            }
        }
    }
    if (!cJSON_IsObject(command)) {
        cJSON_Delete(command);
        return Error(req, "JSON không hợp lệ.");
    }
    const char* op = nullptr;
    std::string uri(req->uri);
    uri.resize(uri.find('?') == std::string::npos ? uri.size() : uri.find('?'));
    if (uri == "/api/tv/channels")
        op = "tv.channels.list";
    else if (uri == "/api/tv/channel")
        op = req->method == HTTP_GET ? "tv.channels.get" : "tv.channels.save";
    else if (uri == "/api/tv/channel/delete")
        op = "tv.channels.delete";
    else if (uri == "/api/tv/check")
        op = "tv.check";
    else if (uri == "/api/tv/play")
        op = "tv.play";
    cJSON_DeleteItemFromObjectCaseSensitive(command, "op");
    cJSON_AddStringToObject(command, "op", op ? op : "invalid");
    char* encoded = cJSON_PrintUnformatted(command);
    std::string payload = encoded ? encoded : "{}";
    cJSON_free(encoded);
    cJSON_Delete(command);
    std::string result, error;
    if (!controller_.RequestService(payload, &result, &error))
        return Error(req, error.c_str());
    return Reply(req, result);
}

}  // namespace mrkenddo
