#ifndef MRKENDDO_SD_WEB_SERVER_H_
#define MRKENDDO_SD_WEB_SERVER_H_

#include "sd_storage.h"

#include <esp_http_server.h>
#include <atomic>
#include <mutex>

namespace mrkenddo {
class SdWebServer {
public:
    static constexpr size_t kHandlerCount = 5;
    explicit SdWebServer(SdStorage& storage);
    ~SdWebServer();
    esp_err_t Register(httpd_handle_t server);
    void StopTransfers();

private:
    SdStorage& storage_;
    std::mutex job_mutex_;
    std::atomic<bool> closing_{false};
    bool busy_ = false;
    SemaphoreHandle_t idle_ = nullptr;
    struct Job {
        SdWebServer* owner;
        httpd_req_t* request;
    };
    static esp_err_t Page(httpd_req_t* req);
    static esp_err_t Handler(httpd_req_t* req);
    static void RunJob(void* context);
    esp_err_t HandleRequest(httpd_req_t* req, bool& body_consumed);
};
}  // namespace mrkenddo
#endif
