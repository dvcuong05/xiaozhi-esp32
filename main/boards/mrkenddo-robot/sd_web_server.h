#ifndef MRKENDDO_SD_WEB_SERVER_H_
#define MRKENDDO_SD_WEB_SERVER_H_

#include "sd_storage.h"

#include <esp_http_server.h>
#include <atomic>
#include <mutex>
#include <string>

namespace mrkenddo {
class SdWebServer {
public:
    static constexpr size_t kHandlerCount = 5;
    explicit SdWebServer(SdStorage& storage);
    ~SdWebServer();
    esp_err_t Register(httpd_handle_t server);
    void StopTransfers();
    SdStorage::Result ShowPhoto(const std::string& path, uint32_t duration_ms = 30000);
    SdStorage::Result StartRandomSlideshow(const std::string& album, uint32_t interval_ms,
                                           size_t max_photos);
    void StopSlideshow();

private:
    SdStorage& storage_;
    std::mutex job_mutex_;
    std::atomic<bool> closing_{false};
    bool busy_ = false;
    SemaphoreHandle_t idle_ = nullptr;
    std::mutex slideshow_mutex_;
    SemaphoreHandle_t slideshow_done_ = nullptr;
    TaskHandle_t slideshow_task_ = nullptr;
    std::atomic<bool> slideshow_running_{false};
    std::string slideshow_album_;
    uint32_t slideshow_interval_ms_ = 15000;
    size_t slideshow_max_photos_ = 10;
    struct Job {
        SdWebServer* owner;
        httpd_req_t* request;
    };
    static esp_err_t Page(httpd_req_t* req);
    static esp_err_t Handler(httpd_req_t* req);
    static void RunJob(void* context);
    static void SlideshowTask(void* context);
    esp_err_t HandleRequest(httpd_req_t* req, bool& body_consumed);
};
}  // namespace mrkenddo
#endif
