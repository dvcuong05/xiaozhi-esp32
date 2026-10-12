#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <string>

class RobotDrive;

// C wrapper function for OLED reinitialization (defined in mrkenddo_robot.cc)
extern "C" void ReinitializeOledDisplay();
extern "C" void ApplyOledDisplaySettings();
extern "C" void ApplyTftRotation(int rotation);

class WebServer {
public:
    WebServer(RobotDrive* drive);
    ~WebServer();

    bool Start(int port = 80);
    void Stop();

private:
    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t ApiPromptHandler(httpd_req_t* req);
    static esp_err_t ApiPromptStatusHandler(httpd_req_t* req);
    static esp_err_t ApiMoveHandler(httpd_req_t* req);
    static esp_err_t ApiStopHandler(httpd_req_t* req);
    static esp_err_t ApiConfigHandler(httpd_req_t* req);
    static esp_err_t ApiOledConfigHandler(httpd_req_t* req);
    static esp_err_t ApiOledTestHandler(httpd_req_t* req);
    static esp_err_t ApiOledMessageHandler(httpd_req_t* req);
    static esp_err_t ApiOledClearHandler(httpd_req_t* req);
    static esp_err_t ApiStatusHandler(httpd_req_t* req);
    static esp_err_t ApiTftConfigHandler(httpd_req_t* req);

    httpd_handle_t server_;
    RobotDrive* drive_;
};

#endif  // WEB_SERVER_H
