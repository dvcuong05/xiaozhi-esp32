#include "sd_web_server.h"
#include "sd_web_ui.h"

#include "application.h"
#include "board.h"
#include "display/display.h"
#include "display/lvgl_display/jpg/jpeg_to_image.h"
#include "display/lvgl_display/lvgl_image.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cJSON.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace mrkenddo {
namespace {
using Result = SdStorage::Result;
using Code = SdStorage::Code;
constexpr char kTag[] = "RobotSD";
std::atomic<bool> g_sd_image_decode_busy{false};
std::atomic<uint32_t> g_sd_image_request_id{0};

void FinishImageTask(std::unique_ptr<struct ShowImageJob>& job);

struct ShowImageJob {
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> jpeg{nullptr, heap_caps_free};
    size_t jpeg_size = 0;
    size_t expected_width = 0;
    size_t expected_height = 0;
    uint32_t duration_ms = 30000;
    uint32_t request_id = 0;
};

void FinishImageTask(std::unique_ptr<ShowImageJob>& job) {
    job.reset();
    g_sd_image_decode_busy.store(false);
    vTaskDelete(nullptr);
}

void DecodeAndShowImage(void* context) {
    std::unique_ptr<ShowImageJob> job(static_cast<ShowImageJob*>(context));
    uint8_t* decoded = nullptr;
    size_t decoded_size = 0, width = 0, height = 0, stride = 0;
    const auto error = jpeg_to_image(job->jpeg.get(), job->jpeg_size, &decoded, &decoded_size,
                                     &width, &height, &stride);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "JPEG decode failed: %s", esp_err_to_name(error));
        FinishImageTask(job);
        return;
    }
    if (width != job->expected_width || height != job->expected_height || decoded_size == 0) {
        heap_caps_free(decoded);
        ESP_LOGW(kTag, "JPEG dimensions changed or decoded output is invalid");
        FinishImageTask(job);
        return;
    }

    auto image = std::make_unique<LvglAllocatedImage>(decoded, decoded_size, width, height, stride,
                                                      LV_COLOR_FORMAT_RGB565);
    if (!image->IsValid()) {
        ESP_LOGW(kTag, "Decoded JPEG could not be wrapped for LVGL");
        image.reset();
        FinishImageTask(job);
        return;
    }
    auto holder = std::make_shared<std::unique_ptr<LvglImage>>(std::move(image));
    const auto request_id = job->request_id;
    const auto duration_ms = job->duration_ms;
    Application::GetInstance().Schedule([holder, request_id, duration_ms]() {
        if (request_id != g_sd_image_request_id.load())
            return;
        auto* display = Board::GetInstance().GetDisplay();
        if (display != nullptr)
            display->SetPreviewImage(std::move(*holder), duration_ms);
    });
    FinishImageTask(job);
}

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

SdWebServer::SdWebServer(SdStorage& storage)
    : storage_(storage),
      idle_(xSemaphoreCreateBinary()),
      slideshow_done_(xSemaphoreCreateBinary()) {}

SdWebServer::~SdWebServer() {
    StopSlideshow();
    StopTransfers();
    if (idle_)
        vSemaphoreDelete(idle_);
    if (slideshow_done_)
        vSemaphoreDelete(slideshow_done_);
}

SdStorage::Result SdWebServer::ShowPhoto(const std::string& path, uint32_t duration_ms) {
    if (duration_ms < 5000 || duration_ms > 120000)
        return {SdStorage::Code::Invalid, "Thời lượng hiển thị phải từ 5 đến 120 giây."};
    bool expected = false;
    if (!g_sd_image_decode_busy.compare_exchange_strong(expected, true))
        return {SdStorage::Code::Busy, "Một ảnh khác đang được giải mã."};
    auto* job = new (std::nothrow) ShowImageJob();
    if (job == nullptr) {
        g_sd_image_decode_busy.store(false);
        return {SdStorage::Code::Unavailable, "Không đủ bộ nhớ để nạp ảnh."};
    }
    job->request_id = ++g_sd_image_request_id;
    uint8_t* jpeg = nullptr;
    size_t jpeg_size = 0, width = 0, height = 0;
    auto result = storage_.ReadPhotoJpeg(path, jpeg, jpeg_size, width, height);
    if (!result) {
        delete job;
        g_sd_image_decode_busy.store(false);
        return result;
    }
    job->jpeg.reset(jpeg);
    job->jpeg_size = jpeg_size;
    job->expected_width = width;
    job->expected_height = height;
    job->duration_ms = duration_ms;
    if (xTaskCreate(DecodeAndShowImage, "sd_image", 16384, job, 2, nullptr) != pdPASS) {
        delete job;
        g_sd_image_decode_busy.store(false);
        return {SdStorage::Code::Unavailable, "Không tạo được task giải mã ảnh."};
    }
    return {};
}

SdStorage::Result SdWebServer::StartRandomSlideshow(const std::string& album, uint32_t interval_ms,
                                                    size_t max_photos) {
    if (album.size() > 96 || interval_ms < 5000 || interval_ms > 60000 || max_photos == 0 ||
        max_photos > 50)
        return {SdStorage::Code::Invalid,
                "Album không hợp lệ; thời gian 5-60 giây, tối đa 50 ảnh."};
    if (slideshow_done_ == nullptr)
        return {SdStorage::Code::Unavailable, "Không đủ tài nguyên để quản lý trình chiếu."};
    SdStorage::Photo sample;
    if (auto result = storage_.RandomPhoto(album, sample); !result)
        return result;
    std::lock_guard<std::mutex> lock(slideshow_mutex_);
    if (slideshow_task_ != nullptr)
        return {SdStorage::Code::Busy, "Đang có album ảnh được trình chiếu."};
    if (slideshow_done_)
        xSemaphoreTake(slideshow_done_, 0);
    slideshow_album_ = album;
    slideshow_interval_ms_ = interval_ms;
    slideshow_max_photos_ = max_photos;
    slideshow_all_photos_ = false;
    slideshow_random_order_ = true;
    slideshow_running_.store(true);
    if (xTaskCreate(SlideshowTask, "sd_slideshow", 6144, this, 2, &slideshow_task_) != pdPASS) {
        slideshow_task_ = nullptr;
        slideshow_running_.store(false);
        return {SdStorage::Code::Unavailable, "Không tạo được tác vụ trình chiếu album."};
    }
    // The task reads the SD index asynchronously; MCP/WSS only waits for this
    // bounded start acknowledgement, never for a full directory scan.
    return {};
}

SdStorage::Result SdWebServer::StartAlbumSlideshow(const std::string& album, uint32_t interval_ms,
                                                   bool random_order, size_t& photo_count,
                                                   bool& truncated) {
    if (album.empty() || album.size() > 96 || album.find('/') != std::string::npos ||
        interval_ms < 5000 || interval_ms > 60000)
        return {SdStorage::Code::Invalid,
                "Album không hợp lệ; thời gian mỗi ảnh phải từ 5 đến 60 giây."};
    if (slideshow_done_ == nullptr)
        return {SdStorage::Code::Unavailable, "Không đủ tài nguyên để quản lý trình chiếu."};

    SdStorage::AlbumPhotoSequence sequence;
    if (auto result = storage_.GetAlbumPhotoSequence(album, sequence); !result)
        return result;
    photo_count = sequence.count;
    truncated = sequence.truncated;

    std::lock_guard<std::mutex> lock(slideshow_mutex_);
    if (slideshow_task_ != nullptr)
        return {SdStorage::Code::Busy, "Đang có album ảnh được trình chiếu."};
    if (slideshow_done_)
        xSemaphoreTake(slideshow_done_, 0);
    slideshow_album_ = album;
    slideshow_interval_ms_ = interval_ms;
    slideshow_max_photos_ = sequence.count;
    slideshow_all_photos_ = true;
    slideshow_random_order_ = random_order;
    slideshow_sequence_ = sequence;
    slideshow_running_.store(true);
    if (xTaskCreate(SlideshowTask, "sd_slideshow", 6144, this, 2, &slideshow_task_) != pdPASS) {
        slideshow_task_ = nullptr;
        slideshow_running_.store(false);
        slideshow_all_photos_ = false;
        return {SdStorage::Code::Unavailable, "Không tạo được tác vụ trình chiếu album."};
    }
    return {};
}

void SdWebServer::SlideshowTask(void* context) {
    auto* server = static_cast<SdWebServer*>(context);
    std::string album;
    uint32_t interval_ms;
    size_t max_photos;
    bool all_photos;
    bool random_order;
    SdStorage::AlbumPhotoSequence sequence;
    {
        std::lock_guard<std::mutex> lock(server->slideshow_mutex_);
        album = server->slideshow_album_;
        interval_ms = server->slideshow_interval_ms_;
        max_photos = server->slideshow_max_photos_;
        all_photos = server->slideshow_all_photos_;
        random_order = server->slideshow_random_order_;
        sequence = server->slideshow_sequence_;
    }
    if (all_photos && random_order) {
        for (size_t i = sequence.count; i > 1; --i) {
            const size_t other = esp_random() % i;
            std::swap(sequence.indexes[i - 1], sequence.indexes[other]);
        }
    }
    for (size_t shown = 0; shown < max_photos && server->slideshow_running_.load(); ++shown) {
        SdStorage::Photo photo;
        const auto result = all_photos
                                ? server->storage_.GetPhotoByIndex(sequence.indexes[shown], photo)
                                : server->storage_.RandomPhoto(album, photo);
        if (!result) {
            ESP_LOGW(kTag, "Slideshow stopped: %s", result.message.c_str());
            break;
        }
        const uint32_t display_ms =
            all_photos ? interval_ms : std::min<uint32_t>(120000, interval_ms + 1000);
        auto show = server->ShowPhoto(photo.path, display_ms);
        for (unsigned retry = 0; !show && show.code == SdStorage::Code::Busy && retry < 20 &&
                                 server->slideshow_running_.load();
             ++retry) {
            vTaskDelay(pdMS_TO_TICKS(250));
            show = server->ShowPhoto(photo.path, display_ms);
        }
        if (!show)
            ESP_LOGW(kTag, "Could not show slideshow image: %s", show.message.c_str());
        for (uint32_t waited = 0; waited < interval_ms && server->slideshow_running_.load();
             waited += 250)
            vTaskDelay(pdMS_TO_TICKS(250));
    }
    server->slideshow_running_.store(false);
    if (all_photos) {
        ++g_sd_image_request_id;
        Application::GetInstance().Schedule([]() {
            auto* display = Board::GetInstance().GetDisplay();
            if (display != nullptr)
                display->SetPreviewImage(nullptr);
        });
    }
    {
        std::lock_guard<std::mutex> lock(server->slideshow_mutex_);
        server->slideshow_task_ = nullptr;
    }
    if (server->slideshow_done_)
        xSemaphoreGive(server->slideshow_done_);
    vTaskDelete(nullptr);
}

void SdWebServer::StopSlideshow() {
    TaskHandle_t task = nullptr;
    {
        std::lock_guard<std::mutex> lock(slideshow_mutex_);
        slideshow_running_.store(false);
        task = slideshow_task_;
    }
    if (task != nullptr && task != xTaskGetCurrentTaskHandle() && slideshow_done_)
        xSemaphoreTake(slideshow_done_, portMAX_DELAY);
    ++g_sd_image_request_id;
    Application::GetInstance().Schedule([]() {
        auto* display = Board::GetInstance().GetDisplay();
        if (display != nullptr)
            display->SetPreviewImage(nullptr);
    });
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
    std::array<char, 1536> query = {};
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
            cJSON_AddStringToObject(root, "music_root", SdStorage::kMusicRoot);
            cJSON_AddStringToObject(root, "music_index_state", status.music_index_state.c_str());
            cJSON_AddStringToObject(root, "music_index_message",
                                    status.music_index_message.c_str());
            cJSON_AddNumberToObject(root, "music_tracks", status.music_tracks);
            cJSON_AddBoolToObject(root, "music_truncated", status.music_truncated);
            cJSON_AddNumberToObject(root, "music_generation", status.music_generation);
        }
        return Json(req, root);
    }
    if (req->method == HTTP_GET && op == "music_search") {
        std::string filters[4];
        const char* keys[] = {"query", "artist", "album", "genre"};
        for (size_t i = 0; i < 4; ++i) {
            if (has_parameter(keys[i]) && !Parameter(query.data(), keys[i], filters[i]))
                return Reply(req, {Code::Invalid, "Bộ lọc nhạc không hợp lệ."});
        }
        std::string sort = "relevance", direction = "asc", missing;
        if ((has_parameter("sort") && !Parameter(query.data(), "sort", sort)) ||
            (has_parameter("direction") && !Parameter(query.data(), "direction", direction)) ||
            (has_parameter("missing") && !Parameter(query.data(), "missing", missing)))
            return Reply(req, {Code::Invalid, "Tùy chọn tìm kiếm nhạc không hợp lệ."});
        if (direction != "asc" && direction != "desc")
            return Reply(req, {Code::Invalid, "direction phải là asc hoặc desc."});
        std::string value;
        uint64_t offset_value = 0, limit_value = 25;
        if ((has_parameter("offset") &&
             (!Parameter(query.data(), "offset", value) ||
              !Number(value, SdStorage::kMaxMusicTracks, offset_value))) ||
            (has_parameter("limit") && (!Parameter(query.data(), "limit", value) ||
                                        !Number(value, 50, limit_value) || limit_value == 0)))
            return Reply(req, {Code::Invalid, "offset/limit không hợp lệ (limit 1-50)."});
        std::vector<SdStorage::MusicTrack> tracks;
        bool truncated = false;
        size_t total = 0;
        if (auto result = storage_.SearchMusic(filters[0], filters[1], filters[2], filters[3],
                                               static_cast<size_t>(limit_value), tracks, truncated,
                                               static_cast<size_t>(offset_value), sort,
                                               direction == "desc", missing, &total);
            !result)
            return Reply(req, result);
        cJSON* root = cJSON_CreateObject();
        cJSON* results = root ? cJSON_AddArrayToObject(root, "results") : nullptr;
        if (!results) {
            cJSON_Delete(root);
            return Reply(req, {Code::Unavailable, "Không đủ bộ nhớ trả kết quả nhạc."});
        }
        cJSON_AddBoolToObject(root, "truncated", truncated);
        cJSON_AddBoolToObject(root, "catalog_truncated", storage_.GetStatus().music_truncated);
        cJSON_AddNumberToObject(root, "offset", offset_value);
        cJSON_AddNumberToObject(root, "limit", limit_value);
        cJSON_AddNumberToObject(root, "total", total);
        cJSON_AddBoolToObject(root, "has_more", offset_value + tracks.size() < total);
        cJSON_AddStringToObject(root, "sort", sort.c_str());
        cJSON_AddStringToObject(root, "direction", direction.c_str());
        for (const auto& track : tracks) {
            cJSON* item = cJSON_CreateObject();
            if (!item)
                continue;
            cJSON_AddStringToObject(item, "path", track.path.c_str());
            cJSON_AddStringToObject(item, "title", track.title.c_str());
            cJSON_AddStringToObject(item, "artist", track.artist.c_str());
            cJSON_AddStringToObject(item, "album", track.album.c_str());
            cJSON_AddStringToObject(item, "genre", track.genre.c_str());
            cJSON_AddNumberToObject(item, "duration_ms", track.duration_ms);
            cJSON_AddItemToArray(results, item);
        }
        return Json(req, root);
    }
    if (req->method == HTTP_GET && op == "music_player") {
        const auto playback = Application::GetInstance().GetLocalMediaPlaybackStatus();
        cJSON* root = cJSON_CreateObject();
        if (!root)
            return Reply(req, {Code::Unavailable, "Player status allocation failed."});
        cJSON_AddBoolToObject(root, "active", playback.active);
        cJSON_AddStringToObject(root, "path", playback.metadata.source_path.c_str());
        cJSON_AddStringToObject(root, "title", playback.metadata.title.c_str());
        cJSON_AddStringToObject(root, "artist", playback.metadata.artist.c_str());
        cJSON_AddNumberToObject(root, "duration_ms", playback.metadata.duration_ms);
        cJSON_AddNumberToObject(root, "position_ms", playback.position_ms);
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
    if (op == "music_metadata" || op == "music_metadata_reset") {
        if (!has_parameter("path"))
            return Reply(req, {Code::Invalid, "Thiếu đường dẫn bài nhạc."});
        std::string artist, album, genre;
        const bool reset = op == "music_metadata_reset";
        if (!reset) {
            const char* keys[] = {"artist", "album", "genre"};
            std::string* values[] = {&artist, &album, &genre};
            for (size_t i = 0; i < 3; ++i) {
                if (!has_parameter(keys[i]) || !Parameter(query.data(), keys[i], *values[i]))
                    return Reply(req, {Code::Invalid, "Cần gửi đủ artist, album và genre."});
            }
        }
        auto result = storage_.SetMusicMetadata(path, artist, album, genre, reset);
        if (!result)
            return Reply(req, result);
        return Reply(req, {Code::Ok, reset ? "Đang khôi phục metadata từ ID3/tên file."
                                           : "Đã lưu metadata sửa tay vào thẻ nhớ."});
    }
    if (op == "music_next") {
        const auto playback = Application::GetInstance().GetLocalMediaPlaybackStatus();
        if (!playback.active || playback.metadata.source_path.empty())
            return Reply(req, {Code::Busy, "No active SD music track."});
        SdStorage::MusicTrack next;
        if (auto result = storage_.NextMusicTrack(playback.metadata.source_path, next); !result)
            return Reply(req, result);
        NotifyPlayer::InputFactory input_factory;
        if (auto result = storage_.CreateAudioInput(next.path, input_factory); !result)
            return Reply(req, result);
        MediaPlaybackMetadata metadata;
        storage_.GetMusicMetadata(next.path, metadata);
        Application::GetInstance().RequestLocalMedia(std::move(metadata), std::move(input_factory));
        return Reply(req, {Code::Ok, "Queued next SD music track."});
    }
    if (op == "play") {
        NotifyPlayer::InputFactory input_factory;
        if (auto result = storage_.CreateAudioInput(path, input_factory); !result)
            return Reply(req, result);
        MediaPlaybackMetadata metadata;
        storage_.GetMusicMetadata(path, metadata);
        Application::GetInstance().RequestLocalMedia(std::move(metadata), std::move(input_factory));
        return Reply(req, {Code::Ok, "Đã xếp file âm thanh vào hàng đợi phát."});
    }
    if (op == "stop") {
        Application::GetInstance().RequestStopMedia();
        return Reply(req, {Code::Ok, "Đã gửi yêu cầu dừng phát media."});
    }
    if (op == "show") {
        bool expected = false;
        if (!g_sd_image_decode_busy.compare_exchange_strong(expected, true))
            return Reply(req, {Code::Busy, "Một ảnh khác đang được giải mã."});
        auto* job = new (std::nothrow) ShowImageJob();
        if (job == nullptr) {
            g_sd_image_decode_busy.store(false);
            return Reply(req, {Code::Unavailable, "Không đủ bộ nhớ để nạp ảnh."});
        }
        job->request_id = ++g_sd_image_request_id;
        uint8_t* jpeg = nullptr;
        size_t jpeg_size = 0, width = 0, height = 0;
        auto result = storage_.ReadJpeg(path, jpeg, jpeg_size, width, height);
        if (!result) {
            delete job;
            g_sd_image_decode_busy.store(false);
            return Reply(req, result);
        }
        job->jpeg.reset(jpeg);
        job->jpeg_size = jpeg_size;
        job->expected_width = width;
        job->expected_height = height;
        job->duration_ms = 30000;
        if (xTaskCreate(DecodeAndShowImage, "sd_image", 16384, job, 2, nullptr) != pdPASS) {
            delete job;
            g_sd_image_decode_busy.store(false);
            return Reply(req, {Code::Unavailable, "Không tạo được task giải mã ảnh."});
        }
        return Reply(req, {Code::Ok, "Đang giải mã ảnh và chuyển lên TFT trong 30 giây."});
    }
    if (op == "slideshow") {
        static constexpr char kPhotoRoot[] = "/robot/images/";
        if (path.compare(0, sizeof(kPhotoRoot) - 1, kPhotoRoot) != 0)
            return Reply(
                req, {Code::Invalid, "Chỉ có thể chiếu album con trực tiếp trong /robot/images."});
        const std::string album = path.substr(sizeof(kPhotoRoot) - 1);
        if (album.empty() || album.find('/') != std::string::npos)
            return Reply(req, {Code::Invalid, "Đường dẫn album không hợp lệ."});

        std::string mode = "sequential";
        if (has_parameter("mode") && !Parameter(query.data(), "mode", mode))
            return Reply(req, {Code::Invalid, "Chế độ trình chiếu không hợp lệ."});
        if (mode != "sequential" && mode != "random")
            return Reply(req,
                         {Code::Invalid, "Chế độ trình chiếu phải là sequential hoặc random."});

        uint64_t interval_seconds = 15;
        if (has_parameter("interval_seconds") &&
            (!Parameter(query.data(), "interval_seconds", value) ||
             !Number(value, 60, interval_seconds) || interval_seconds < 5))
            return Reply(req, {Code::Invalid, "Thời gian mỗi ảnh phải từ 5 đến 60 giây."});

        size_t photo_count = 0;
        bool truncated = false;
        if (auto result = StartAlbumSlideshow(album, interval_seconds * 1000, mode == "random",
                                              photo_count, truncated);
            !result)
            return Reply(req, result);
        std::string message = "Đang trình chiếu " + std::to_string(photo_count) +
                              " ảnh theo thứ tự " + (mode == "random" ? "ngẫu nhiên" : "tuần tự") +
                              ".";
        if (truncated)
            message += " Chỉ mục ảnh đã bị cắt; có thể còn ảnh chưa được trình chiếu.";
        return Reply(req, {Code::Ok, message});
    }
    if (op == "slideshow_stop") {
        StopSlideshow();
        return Reply(req, {Code::Ok, "Đã dừng trình chiếu và quay lại màn hình robot."});
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
