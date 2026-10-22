#include "sd_web_server.h"
#include "sd_web_ui.h"

#include "application.h"

#include <esp_heap_caps.h>
#include <cJSON.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace mrkenddo {
namespace {
using Result = SdStorage::Result;
using Code = SdStorage::Code;

esp_err_t Json(httpd_req_t* req, cJSON* value) {
    if (!value)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    char* data = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!data)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    auto result = httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN);
    cJSON_free(data);
    return result;
}

esp_err_t Reply(httpd_req_t* req, Result result) {
    const char* status = "200 OK";
    if (result.code == Code::Busy || result.code == Code::Unavailable)
        status = "503 Service Unavailable";
    else if (result.code == Code::Invalid)
        status = "400 Bad Request";
    else if (result.code == Code::NotFound)
        status = "404 Not Found";
    else if (result.code == Code::Exists)
        status = "409 Conflict";
    else if (result.code == Code::Limit)
        status = "413 Content Too Large";
    else if (result.code == Code::Io)
        status = "500 Internal Server Error";
    httpd_resp_set_status(req, status);
    cJSON* root = cJSON_CreateObject();
    if (root) {
        cJSON_AddBoolToObject(root, "ok", static_cast<bool>(result));
        cJSON_AddStringToObject(root, result ? "message" : "error", result.message.c_str());
    }
    return Json(req, root);
}

// esp_http_server's query helper does not URL-decode parameter values.
bool Parameter(const char* query, const char* key, std::string& result) {
    std::array<char, 768> encoded = {};
    if (httpd_query_key_value(query, key, encoded.data(), encoded.size()) != ESP_OK)
        return false;
    auto hex = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    result.clear();
    for (size_t i = 0; encoded[i]; ++i) {
        unsigned char c = encoded[i];
        if (c == '%') {
            if (!encoded[i + 1] || !encoded[i + 2])
                return false;
            int a = hex(encoded[i + 1]), b = hex(encoded[i + 2]);
            if (a < 0 || b < 0)
                return false;
            c = static_cast<unsigned char>((a << 4) | b);
            i += 2;
        } else if (c == '+')
            c = ' ';
        if (!c)
            return false;
        result.push_back(static_cast<char>(c));
    }
    return true;
}

bool Number(const std::string& text, uint64_t maximum, uint64_t& result) {
    if (text.empty())
        return false;
    result = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9' || result > maximum / 10)
            return false;
        result = result * 10 + c - '0';
        if (result > maximum)
            return false;
    }
    return true;
}

std::string Attachment(const std::string& path) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string value = "attachment; filename*=UTF-8''";
    for (unsigned char c : path.substr(path.find_last_of('/') + 1)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')
            value += c;
        else {
            value += '%';
            value += hex[c >> 4];
            value += hex[c & 15];
        }
    }
    return value;
}
}  // namespace

SdWebServer::SdWebServer(SdStorage& storage) : storage_(storage), idle_(xSemaphoreCreateBinary()) {}

SdWebServer::~SdWebServer() {
    StopTransfers();
    if (idle_)
        vSemaphoreDelete(idle_);
}

esp_err_t SdWebServer::Register(httpd_handle_t server) {
    closing_.store(false);
    httpd_uri_t routes[] = {
        {"/storage", HTTP_GET, Page, this},
        {"/api/storage", HTTP_GET, Handler, this},
        {"/api/storage", HTTP_POST, Handler, this},
        {"/api/storage/upload", HTTP_POST, Handler, this},
        {"/api/storage/download", HTTP_GET, Handler, this},
    };
    static_assert(sizeof(routes) / sizeof(routes[0]) == kHandlerCount);
    for (auto& route : routes) {
        auto err = httpd_register_uri_handler(server, &route);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}

void SdWebServer::StopTransfers() {
    bool wait = false;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        closing_.store(true);
        wait = busy_;
    }
    if (wait && idle_)
        xSemaphoreTake(idle_, portMAX_DELAY);
    std::lock_guard<std::mutex> lock(job_mutex_);
}

esp_err_t SdWebServer::Page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, web_ui::kStorageHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t SdWebServer::Handler(httpd_req_t* req) {
    auto& self = *static_cast<SdWebServer*>(req->user_ctx);
    // A single short-lived file worker keeps uploads/downloads and slow SD I/O
    // off the HTTP control task, leaving /api/stop and the dashboard responsive.
    std::lock_guard<std::mutex> lock(self.job_mutex_);
    if (self.closing_.load() || self.busy_) {
        auto result = Reply(req, {Code::Busy, "Đang truyền file. Hãy thử lại sau."});
        return req->content_len ? ESP_FAIL : result;
    }
    if (!self.idle_)
        return Reply(req, {Code::Unavailable, "Không đủ bộ nhớ."});
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK)
        return ESP_FAIL;
    auto* job = new (std::nothrow) Job{&self, copy};
    self.busy_ = true;
    xSemaphoreTake(self.idle_, 0);
    if (!job || xTaskCreate(RunJob, "sd_http", 8192, job, 2, nullptr) != pdPASS) {
        delete job;
        self.busy_ = false;
        const bool close = copy->content_len != 0;
        const auto server = copy->handle;
        const int socket = httpd_req_to_sockfd(copy);
        Reply(copy, {Code::Unavailable, "Không đủ bộ nhớ để truyền file."});
        httpd_req_async_handler_complete(copy);
        if (close)
            httpd_sess_trigger_close(server, socket);
    }
    return ESP_OK;
}

void SdWebServer::RunJob(void* context) {
    std::unique_ptr<Job> job(static_cast<Job*>(context));
    auto& self = *job->owner;
    bool body_consumed = job->request->content_len == 0;
    auto result = self.HandleRequest(job->request, body_consumed);
    auto handle = job->request->handle;
    int socket = httpd_req_to_sockfd(job->request);
    httpd_req_async_handler_complete(job->request);
    if (result != ESP_OK || !body_consumed)
        httpd_sess_trigger_close(handle, socket);
    {
        std::lock_guard<std::mutex> lock(self.job_mutex_);
        self.busy_ = false;
        xSemaphoreGive(self.idle_);
    }
    job.reset();
    vTaskDelete(nullptr);
}

esp_err_t SdWebServer::HandleRequest(httpd_req_t* req, bool& body_consumed) {
    std::array<char, 1024> query = {};
    if (httpd_req_get_url_query_len(req) >= query.size())
        return Reply(req, {Code::Invalid, "Query quá dài."});
    httpd_req_get_url_query_str(req, query.data(), query.size());
    std::string op = "status", path = "/", value;
    auto has_parameter = [&](const char* key) {
        char probe[2] = {};
        return httpd_query_key_value(query.data(), key, probe, sizeof(probe)) != ESP_ERR_NOT_FOUND;
    };
    if (std::strncmp(req->uri, "/api/storage/upload", 19) == 0)
        op = "upload";
    else if (std::strncmp(req->uri, "/api/storage/download", 21) == 0)
        op = "download";
    else if (has_parameter("op")) {
        if (!Parameter(query.data(), "op", op))
            return Reply(req, {Code::Invalid, "Thiếu thao tác hợp lệ."});
    }
    // Parameter() also rejects decoded NUL, so no path truncation reaches FATFS.
    if (has_parameter("path")) {
        if (!Parameter(query.data(), "path", path))
            return Reply(req, {Code::Invalid, "Đường dẫn không hợp lệ."});
    }
    if (op != "upload" && req->content_len != 0)
        return Reply(req, {Code::Invalid, "Thao tác này không nhận request body."});
    if (req->method == HTTP_GET && op == "status") {
        auto status = storage_.GetStatus();
        cJSON* root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "state", status.state.c_str());
            cJSON_AddStringToObject(root, "message", status.message.c_str());
            cJSON_AddNumberToObject(root, "total_bytes", status.total_bytes);
            cJSON_AddNumberToObject(root, "free_bytes", status.free_bytes);
            cJSON_AddBoolToObject(root, "space_valid", status.space_valid);
            cJSON_AddStringToObject(root, "upload_path", status.upload_path.c_str());
            cJSON_AddNumberToObject(root, "uploaded_bytes", status.uploaded_bytes);
            cJSON_AddNumberToObject(root, "max_file_bytes", SdStorage::kMaxFileBytes);
            cJSON_AddNumberToObject(root, "chunk_bytes", SdStorage::kUploadChunkBytes);
        }
        return Json(req, root);
    }
    if (req->method == HTTP_GET && op == "list") {
        uint64_t offset = 0;
        if (has_parameter("offset") &&
            (!Parameter(query.data(), "offset", value) || !Number(value, 10000, offset)))
            return Reply(req, {Code::Invalid, "Offset không hợp lệ."});
        SdStorage::Listing listing;
        if (auto result = storage_.List(path, offset, listing); !result)
            return Reply(req, result);
        cJSON* root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "path", path.c_str());
            cJSON_AddBoolToObject(root, "more", listing.more);
            cJSON_AddNumberToObject(root, "next_offset", listing.next_offset);
            cJSON* items = cJSON_AddArrayToObject(root, "entries");
            if (!items) {
                cJSON_Delete(root);
                return Reply(req, {Code::Unavailable, "Không đủ bộ nhớ để liệt kê file."});
            }
            for (const auto& entry : listing.entries) {
                cJSON* item = cJSON_CreateObject();
                if (!item)
                    continue;
                cJSON_AddStringToObject(item, "name", entry.name.c_str());
                cJSON_AddBoolToObject(item, "directory", entry.directory);
                cJSON_AddNumberToObject(item, "size", entry.size);
                cJSON_AddItemToArray(items, item);
            }
        }
        return Json(req, root);
    }
    if (req->method == HTTP_GET && op == "download") {
        bool started = false;
        const auto attachment = Attachment(path);
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", attachment.c_str());
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        auto result = storage_.Download(path, [&](const char* data, size_t size) {
            if (closing_.load())
                return false;
            started = true;
            return httpd_resp_send_chunk(req, data, size) == ESP_OK;
        });
        if (!result)
            return started ? ESP_FAIL : Reply(req, result);
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    if (req->method != HTTP_POST)
        return Reply(req, {Code::Invalid, "Thao tác không hợp lệ."});
    if (op == "play") {
        NotifyPlayer::InputFactory input_factory;
        if (auto result = storage_.CreateAudioInput(path, input_factory); !result)
            return Reply(req, result);
        MediaPlaybackMetadata metadata;
        metadata.title = path.substr(path.find_last_of('/') + 1);
        Application::GetInstance().RequestLocalMedia(std::move(metadata), std::move(input_factory));
        return Reply(req, {Code::Ok, "Đã xếp file âm thanh vào hàng đợi phát."});
    }
    if (op == "stop") {
        Application::GetInstance().RequestStopMedia();
        return Reply(req, {Code::Ok, "Đã gửi yêu cầu dừng phát media."});
    }
    if (op == "upload") {
        uint64_t offset = 0, total = 0;
        if (!Parameter(query.data(), "offset", value) ||
            !Number(value, SdStorage::kMaxFileBytes, offset) ||
            !Parameter(query.data(), "total", value) ||
            !Number(value, SdStorage::kMaxFileBytes, total) ||
            req->content_len > SdStorage::kUploadChunkBytes)
            return Reply(req, {Code::Limit, "Upload không hợp lệ hoặc khối vượt 1 MiB."});
        std::unique_ptr<char, decltype(&heap_caps_free)> chunk(
            req->content_len == 0 ? nullptr
                                  : static_cast<char*>(heap_caps_malloc(
                                        req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
            heap_caps_free);
        if (req->content_len != 0 && !chunk)
            return Reply(req, {Code::Unavailable, "Không đủ PSRAM cho khối upload 1 MiB."});
        size_t received = 0;
        while (received < req->content_len) {
            if (closing_.load())
                return ESP_FAIL;
            int bytes = httpd_req_recv(req, chunk.get() + received, req->content_len - received);
            if (bytes <= 0)
                return ESP_FAIL;
            received += bytes;
        }
        body_consumed = true;
        return Reply(req, storage_.Upload(path, offset, total, chunk.get(), req->content_len));
    }
    if (op == "mkdir")
        return Reply(req, storage_.MakeDirectory(path));
    if (op == "delete")
        return Reply(req, storage_.Remove(path));
    if (op == "abort_upload")
        return Reply(req, storage_.AbortUpload(path));
    return Reply(req, storage_.Request(op));
}
}  // namespace mrkenddo
