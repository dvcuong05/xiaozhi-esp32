#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <string>

namespace mrkenddo {
class RobotController;
}  // namespace mrkenddo

class WebServer {
public:
    explicit WebServer(mrkenddo::RobotController& controller);
    ~WebServer();

    bool Start(int port = 80);
    void Stop();

private:
    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t ApiNotifyHandler(httpd_req_t* req);
    static esp_err_t ApiNotifyStopHandler(httpd_req_t* req);
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
    static esp_err_t ApiBootConfigHandler(httpd_req_t* req);
    static esp_err_t ApiQrConfigHandler(httpd_req_t* req);
    static esp_err_t ApiQrShowHandler(httpd_req_t* req);

    httpd_handle_t server_;
    mrkenddo::RobotController& controller_;
};

#endif  // WEB_SERVER_H
