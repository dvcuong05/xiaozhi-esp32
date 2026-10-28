#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mrkenddo {
class RobotController;
class SdWebServer;
class TvWebServer;
class RobotTvController;
}  // namespace mrkenddo

class WebServerLogStream;

class WebServer {
public:
    WebServer(mrkenddo::RobotController& controller, mrkenddo::RobotTvController& tv);
    ~WebServer();

    bool Start(int port = 80);
    void Stop();
    bool ShowSdPhoto(const std::string& path, uint32_t duration_ms, std::string& error);
    bool StartSdSlideshow(const std::string& album, uint32_t interval_ms, size_t max_photos,
                          std::string& error);
    void StopSdSlideshow();

private:
    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t TimePageHandler(httpd_req_t* req);
    static esp_err_t TimeApiHandler(httpd_req_t* req);
    static esp_err_t ApiNotifyHandler(httpd_req_t* req);
    static esp_err_t ApiNotifyStopHandler(httpd_req_t* req);
    static esp_err_t ApiMediaPlayHandler(httpd_req_t* req);
    static esp_err_t ApiMediaStopHandler(httpd_req_t* req);
    static esp_err_t ApiRadioStartHandler(httpd_req_t* req);
    static esp_err_t ApiRadioStopHandler(httpd_req_t* req);
    static esp_err_t ApiRadioStatusHandler(httpd_req_t* req);
    static esp_err_t ApiPromptHandler(httpd_req_t* req);
    static esp_err_t ApiPromptStatusHandler(httpd_req_t* req);
    static esp_err_t ApiMoveHandler(httpd_req_t* req);
    static esp_err_t ApiStopHandler(httpd_req_t* req);
    static esp_err_t ApiRoutineHandler(httpd_req_t* req);
    static esp_err_t ApiMotionStatusHandler(httpd_req_t* req);
    static esp_err_t ApiConfigHandler(httpd_req_t* req);
    static esp_err_t ApiOledConfigHandler(httpd_req_t* req);
    static esp_err_t ApiOledTestHandler(httpd_req_t* req);
    static esp_err_t ApiOledMessageHandler(httpd_req_t* req);
    static esp_err_t ApiOledClearHandler(httpd_req_t* req);
    static esp_err_t ApiStatusHandler(httpd_req_t* req);
    static esp_err_t ApiTftConfigHandler(httpd_req_t* req);
    static esp_err_t ApiFaceConfigHandler(httpd_req_t* req);
    static esp_err_t ApiFacePreviewHandler(httpd_req_t* req);
    static esp_err_t ApiEmotionPreviewHandler(httpd_req_t* req);
    static esp_err_t ApiBootConfigHandler(httpd_req_t* req);
    static esp_err_t ApiQrConfigHandler(httpd_req_t* req);
    static esp_err_t ApiQrShowHandler(httpd_req_t* req);
    static esp_err_t ApiVolumeHandler(httpd_req_t* req);
    static esp_err_t LogWebSocketHandler(httpd_req_t* req);

    httpd_handle_t server_;
    mrkenddo::RobotController& controller_;
    std::unique_ptr<WebServerLogStream> log_stream_;
    std::unique_ptr<mrkenddo::SdWebServer> sd_web_;
    std::unique_ptr<mrkenddo::TvWebServer> tv_web_;
};

#endif  // WEB_SERVER_H
