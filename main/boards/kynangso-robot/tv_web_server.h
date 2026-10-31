#pragma once

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mutex>

namespace kynangso {
class RobotTvController;

class TvWebServer {
public:
    static constexpr size_t kHandlerCount = 9;
    explicit TvWebServer(RobotTvController& controller);
    ~TvWebServer();
    esp_err_t Register(httpd_handle_t server);
    void Stop();

private:
    struct Job;
    static esp_err_t Page(httpd_req_t* req);
    static esp_err_t Status(httpd_req_t* req);
    static esp_err_t StopPlayback(httpd_req_t* req);
    static esp_err_t Handle(httpd_req_t* req);
    static void RunJob(void* context);
    esp_err_t Request(httpd_req_t* req);
    RobotTvController& controller_;
    std::mutex mutex_;
    bool busy_ = false, closing_ = false;
    SemaphoreHandle_t idle_ = nullptr;
};
}  // namespace kynangso
