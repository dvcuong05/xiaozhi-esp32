#include "web_server.h"
#include "application.h"
#include "config.h"
#include "stepper_motor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/soc_caps.h>

#define TAG "WEB_SERVER"

// Embedded HTML page
static const char* INDEX_HTML =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "    <title>Robot Control</title>\n"
    "    <style>\n"
    "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
    "        body { font-family: Arial, sans-serif; max-width: 600px; margin: 0 auto; padding: "
    "20px; background: #f5f5f5; }\n"
    "        .container { background: white; border-radius: 10px; padding: 20px; box-shadow: 0 2px "
    "10px rgba(0,0,0,0.1); }\n"
    "        h1 { color: #333; margin-bottom: 20px; text-align: center; }\n"
    "        .section { margin-bottom: 20px; }\n"
    "        .section h2 { color: #666; font-size: 16px; margin-bottom: 10px; }\n"
    "        textarea { width: 100%; height: 80px; padding: 10px; border: 1px solid #ddd; "
    "border-radius: 5px; resize: vertical; }\n"
    "        button { padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; "
    "font-size: 14px; margin: 5px; }\n"
    "        .btn-primary { background: #007bff; color: white; }\n"
    "        .btn-danger { background: #dc3545; color: white; }\n"
    "        .btn-control { background: #28a745; color: white; width: 80px; height: 80px; "
    "font-size: 24px; margin: 5px; }\n"
    "        .controls { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; "
    "max-width: 300px; margin: 0 auto; }\n"
    "        .controls .empty { visibility: hidden; }\n"
    "        .response { background: #f8f9fa; padding: 10px; border-radius: 5px; margin-top: 10px; "
    "min-height: 50px; }\n"
    "        .status { color: #666; font-size: 12px; margin-top: 5px; }\n"
    "        .config-row { display: flex; align-items: center; margin-bottom: 10px; }\n"
    "        .config-row label { flex: 1; color: #666; }\n"
    "        .config-row input { width: 80px; padding: 5px; border: 1px solid #ddd; border-radius: "
    "5px; }\n"
    "        .btn-save { background: #6c757d; color: white; }\n"
    "        .btn-test { background: #007bff; color: white; margin-left: 8px; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <h1>Robot Control</h1>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h2>Chat</h2>\n"
    "            <textarea id=\"prompt\" placeholder=\"Type your message here...\"></textarea>\n"
    "            <button class=\"btn-primary\" onclick=\"sendPrompt()\">Send</button>\n"
    "            <div class=\"response\" id=\"response\"></div>\n"
    "            <div class=\"status\" id=\"status\"></div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h2>Motor Control</h2>\n"
    "            <div class=\"controls\">\n"
    "                <div class=\"empty\"></div>\n"
    "                <button class=\"btn-control\" onclick=\"move('forward')\">UP</button>\n"
    "                <div class=\"empty\"></div>\n"
    "                <button class=\"btn-control\" onclick=\"move('left')\">LEFT</button>\n"
    "                <button class=\"btn-danger\" onclick=\"stop()\">STOP</button>\n"
    "                <button class=\"btn-control\" onclick=\"move('right')\">RIGHT</button>\n"
    "                <div class=\"empty\"></div>\n"
    "                <button class=\"btn-control\" onclick=\"move('backward')\">DOWN</button>\n"
    "                <div class=\"empty\"></div>\n"
    "            </div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h2>Motor Configuration</h2>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Steps per move:</label>\n"
    "                <input type=\"number\" id=\"steps\" value=\"512\" min=\"100\" max=\"30000\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Delay (ms, lower=faster):</label>\n"
    "                <input type=\"number\" id=\"delay\" value=\"1\" min=\"1\" max=\"100\">\n"
    "            </div>\n"
    "            <button class=\"btn-save\" onclick=\"saveConfig()\">Save Configuration</button>\n"
    "            <div class=\"status\" id=\"configStatus\"></div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h2>OLED Display Configuration</h2>\n"
    "            <div class=\"config-row\">\n"
    "                <label>I2C Address (hex):</label>\n"
    "                <input type=\"text\" id=\"oled_addr\" value=\"3C\" maxlength=\"2\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>SDA GPIO:</label>\n"
    "                <input type=\"number\" id=\"oled_sda\" value=\"2\" min=\"0\" max=\"48\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>SCL GPIO:</label>\n"
    "                <input type=\"number\" id=\"oled_scl\" value=\"1\" min=\"0\" max=\"48\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Width:</label>\n"
    "                <input type=\"number\" id=\"oled_width\" value=\"128\" min=\"64\" "
    "max=\"256\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Height:</label>\n"
    "                <input type=\"number\" id=\"oled_height\" value=\"32\" min=\"16\" "
    "max=\"64\">\n"
    "            </div>\n"
    "            <button class=\"btn-save\" onclick=\"saveOledConfig()\">Save OLED "
    "Config</button>\n"
    "            <button class=\"btn-test\" onclick=\"testOled()\">Test Display</button>\n"
    "            <div class=\"status\" id=\"oledStatus\"></div>\n"
    "        </div>\n"
    "    </div>\n"
    "\n"
    "    <script>\n"
    "        // Load configuration on page load\n"
    "        async function loadConfig() {\n"
    "            try {\n"
    "                const res = await fetch('/api/config', { method: 'GET' });\n"
    "                if (res.ok) {\n"
    "                    const data = await res.json();\n"
    "                    if (data.steps) document.getElementById('steps').value = data.steps;\n"
    "                    if (data.delay_ms) document.getElementById('delay').value = "
    "data.delay_ms;\n"
    "                }\n"
    "            } catch (e) {\n"
    "                console.log('Could not load config, using defaults');\n"
    "            }\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/oled_config', { method: 'GET' });\n"
    "                if (res.ok) {\n"
    "                    const data = await res.json();\n"
    "                    if (data.addr) document.getElementById('oled_addr').value = data.addr;\n"
    "                    if (data.sda) document.getElementById('oled_sda').value = data.sda;\n"
    "                    if (data.scl) document.getElementById('oled_scl').value = data.scl;\n"
    "                    if (data.width) document.getElementById('oled_width').value = "
    "data.width;\n"
    "                    if (data.height) document.getElementById('oled_height').value = "
    "data.height;\n"
    "                }\n"
    "            } catch (e) {\n"
    "                console.log('Could not load OLED config, using defaults');\n"
    "            }\n"
    "        }\n"
    "        \n"
    "        loadConfig();\n"
    "        \n"
    "        async function sendPrompt() {\n"
    "            const prompt = document.getElementById('prompt').value;\n"
    "            const status = document.getElementById('status');\n"
    "            const response = document.getElementById('response');\n"
    "            \n"
    "            if (!prompt.trim()) return;\n"
    "            \n"
    "            status.textContent = 'Sending...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/prompt', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ prompt: prompt })\n"
    "                });\n"
    "                const data = await res.json();\n"
    "                response.textContent = data.result || data.error || 'Sent';\n"
    "                status.textContent = '';\n"
    "                document.getElementById('prompt').value = '';\n"
    "            } catch (e) {\n"
    "                response.textContent = 'Error: ' + e.message;\n"
    "                status.textContent = '';\n"
    "            }\n"
    "        }\n"
    "\n"
    "        async function move(direction) {\n"
    "            const status = document.getElementById('status');\n"
    "            const steps = parseInt(document.getElementById('steps').value);\n"
    "            const delay = parseInt(document.getElementById('delay').value);\n"
    "            status.textContent = 'Moving ' + direction + '...';\n"
    "            \n"
    "            try {\n"
    "                const moveSteps = direction === 'forward' ? steps : \n"
    "                                  direction === 'backward' ? -steps :\n"
    "                                  direction === 'left' ? -steps/2 : steps/2;\n"
    "                const res = await fetch('/api/move', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ steps: moveSteps, delay_ms: delay })\n"
    "                });\n"
    "                const data = await res.json();\n"
    "                status.textContent = data.result || data.error;\n"
    "                setTimeout(() => status.textContent = '', 1000);\n"
    "            } catch (e) {\n"
    "                status.textContent = 'Error: ' + e.message;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        async function stop() {\n"
    "            const status = document.getElementById('status');\n"
    "            status.textContent = 'Stopping...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/stop', { method: 'POST' });\n"
    "                const data = await res.json();\n"
    "                status.textContent = data.result || data.error;\n"
    "                setTimeout(() => status.textContent = '', 1000);\n"
    "            } catch (e) {\n"
    "                status.textContent = 'Error: ' + e.message;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        async function saveConfig() {\n"
    "            const status = document.getElementById('configStatus');\n"
    "            const steps = document.getElementById('steps').value;\n"
    "            const delay = document.getElementById('delay').value;\n"
    "            status.textContent = 'Saving...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/config', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ steps: parseInt(steps), delay_ms: parseInt(delay) "
    "})\n"
    "                });\n"
    "                const data = await res.json();\n"
    "                status.textContent = data.result || data.error;\n"
    "                setTimeout(() => status.textContent = '', 2000);\n"
    "            } catch (e) {\n"
    "                status.textContent = 'Error: ' + e.message;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        async function saveOledConfig() {\n"
    "            const status = document.getElementById('oledStatus');\n"
    "            const addr = document.getElementById('oled_addr').value;\n"
    "            const sda = document.getElementById('oled_sda').value;\n"
    "            const scl = document.getElementById('oled_scl').value;\n"
    "            const width = document.getElementById('oled_width').value;\n"
    "            const height = document.getElementById('oled_height').value;\n"
    "            status.textContent = 'Saving...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/oled_config', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ \n"
    "                        addr: addr, \n"
    "                        sda: parseInt(sda), \n"
    "                        scl: parseInt(scl), \n"
    "                        width: parseInt(width), \n"
    "                        height: parseInt(height) \n"
    "                    })\n"
    "                });\n"
    "                const data = await res.json();\n"
    "                status.textContent = data.result || data.error;\n"
    "                setTimeout(() => status.textContent = '', 2000);\n"
    "            } catch (e) {\n"
    "                status.textContent = 'Error: ' + e.message;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        async function testOled() {\n"
    "            const status = document.getElementById('oledStatus');\n"
    "            status.textContent = 'Testing display...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/oled_test', { method: 'POST' });\n"
    "                const data = await res.json();\n"
    "                status.textContent = data.result || data.error;\n"
    "                setTimeout(() => status.textContent = '', 3000);\n"
    "            } catch (e) {\n"
    "                status.textContent = 'Error: ' + e.message;\n"
    "            }\n"
    "        }\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";

namespace {

constexpr size_t kMaxJsonBodyBytes = 2048;
constexpr int kDefaultMotorSteps = 512;
constexpr int kDefaultMotorDelayMs = 1;
constexpr int kMinOledAddress = 0x03;
constexpr int kMaxOledAddress = 0x77;

struct OledDefaults {
    const char* address;
    int sda;
    int scl;
    int width;
    int height;
};

OledDefaults GetOledDefaults() {
    static char address[3] = {
        static_cast<char>('0' + ((SECOND_DISPLAY_I2C_ADDRESS >> 4) & 0x0f)),
        static_cast<char>('0' + (SECOND_DISPLAY_I2C_ADDRESS & 0x0f)),
        '\0',
    };
    for (size_t i = 0; i < 2; ++i) {
        char& value = address[i];
        if (value >= '0' && value <= '9') {
            continue;
        }
        value = static_cast<char>('A' + (value - '0' - 10));
    }
    return {address, SECOND_DISPLAY_SDA_PIN, SECOND_DISPLAY_SCL_PIN, SECOND_DISPLAY_WIDTH,
            SECOND_DISPLAY_HEIGHT};
}

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

bool ValidOledPins(int sda, int scl) {
    return sda >= 0 && scl >= 0 && sda < SOC_GPIO_PIN_COUNT && scl < SOC_GPIO_PIN_COUNT &&
           sda != scl;
}

bool ValidStoredOledConfig(const char* address, int sda, int scl, int width, int height) {
    if (address == nullptr || std::strlen(address) == 0 || std::strlen(address) > 2) {
        return false;
    }
    char* end = nullptr;
    long parsed_address = std::strtol(address, &end, 16);
    return end != address && *end == '\0' && parsed_address >= kMinOledAddress &&
           parsed_address <= kMaxOledAddress && ValidOledPins(sda, scl) && width >= 64 &&
           width <= 256 && width % 8 == 0 && height >= 16 && height <= 64 && height % 8 == 0;
}

WebServer* GetServer(httpd_req_t* req) { return static_cast<WebServer*>(req->user_ctx); }

}  // namespace

WebServer::WebServer(StepperMotor* stepper_motor)
    : server_(nullptr), stepper_motor_(stepper_motor) {}

WebServer::~WebServer() { Stop(); }

bool WebServer::Start(int port) {
    if (port < 1 || port > 65535 || server_ != nullptr) {
        ESP_LOGE(TAG, "Cong HTTP khong hop le hoac server da chay: %d", port);
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong the khoi dong HTTP server: %s", esp_err_to_name(ret));
        return false;
    }

    httpd_uri_t uris[] = {
        {"/", HTTP_GET, RootHandler, this},
        {"/api/prompt", HTTP_POST, ApiPromptHandler, this},
        {"/api/move", HTTP_POST, ApiMoveHandler, this},
        {"/api/stop", HTTP_POST, ApiStopHandler, this},
        {"/api/config", HTTP_POST, ApiConfigHandler, this},
        {"/api/config", HTTP_GET, ApiConfigHandler, this},
        {"/api/oled_config", HTTP_POST, ApiOledConfigHandler, this},
        {"/api/oled_config", HTTP_GET, ApiOledConfigHandler, this},
        {"/api/oled_test", HTTP_POST, ApiOledTestHandler, this},
    };
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
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGI(TAG, "HTTP server da dung");
    }
}

esp_err_t WebServer::RootHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::ApiPromptHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    const cJSON* prompt_json = cJSON_GetObjectItem(root, "prompt");
    if (prompt_json == nullptr || !cJSON_IsString(prompt_json) ||
        prompt_json->valuestring == nullptr || prompt_json->valuestring[0] == '\0' ||
        std::strlen(prompt_json->valuestring) > 1000) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid prompt");
        return ESP_FAIL;
    }

    cJSON* message_json = cJSON_CreateObject();
    if (message_json == nullptr ||
        cJSON_AddStringToObject(message_json, "type", "text") == nullptr ||
        cJSON_AddStringToObject(message_json, "text", prompt_json->valuestring) == nullptr) {
        cJSON_Delete(message_json);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    char* message_str = cJSON_PrintUnformatted(message_json);
    cJSON_Delete(message_json);
    cJSON_Delete(root);
    if (message_str == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON");
        return ESP_FAIL;
    }
    Application::GetInstance().SendMcpMessage(std::string(message_str));
    cJSON_free(message_str);

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Prompt sent");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMoveHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->stepper_motor_ == nullptr) {
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
    bool valid =
        GetInteger(root, "steps", -StepperMotor::kMaxSteps, StepperMotor::kMaxSteps, &steps) &&
        GetInteger(root, "delay_ms", StepperMotor::kMinDelayMs, StepperMotor::kMaxDelayMs,
                   &delay_ms, false);
    // Keep the historical optional delay_ms API, but reject a malformed value
    // when the field is present.
    if (valid && cJSON_GetObjectItem(root, "delay_ms") != nullptr) {
        valid = GetInteger(root, "delay_ms", StepperMotor::kMinDelayMs, StepperMotor::kMaxDelayMs,
                           &delay_ms);
    }
    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    cJSON_Delete(root);

    server->stepper_motor_->Step(steps, delay_ms);
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Moving");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiStopHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->stepper_motor_ == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    server->stepper_motor_->Stop();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Stopped");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiConfigHandler(httpd_req_t* req) {
    int32_t steps = kDefaultMotorSteps;
    int32_t delay_ms = kDefaultMotorDelayMs;
    if (req->method == HTTP_GET) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("motor_config", NVS_READONLY, &handle);
        if (err == ESP_OK) {
            if (nvs_get_i32(handle, "steps", &steps) != ESP_OK ||
                nvs_get_i32(handle, "delay_ms", &delay_ms) != ESP_OK) {
                ESP_LOGW(TAG, "Cau hinh dong co trong NVS khong day du");
                steps = kDefaultMotorSteps;
                delay_ms = kDefaultMotorDelayMs;
            }
            nvs_close(handle);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Khong the mo NVS dong co: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read NVS");
            return ESP_FAIL;
        }
        steps = std::clamp<int32_t>(steps, 1, StepperMotor::kMaxSteps);
        delay_ms =
            std::clamp<int32_t>(delay_ms, StepperMotor::kMinDelayMs, StepperMotor::kMaxDelayMs);
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr) {
            cJSON_AddNumberToObject(response, "steps", steps);
            cJSON_AddNumberToObject(response, "delay_ms", delay_ms);
        }
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    int saved_steps = 0;
    int saved_delay_ms = 0;
    if (!GetInteger(root, "steps", 1, StepperMotor::kMaxSteps, &saved_steps) ||
        !GetInteger(root, "delay_ms", StepperMotor::kMinDelayMs, StepperMotor::kMaxDelayMs,
                    &saved_delay_ms)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    cJSON_Delete(root);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("motor_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        return ESP_FAIL;
    }
    err = nvs_set_i32(handle, "steps", saved_steps);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "delay_ms", saved_delay_ms);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the luu cau hinh dong co: %s", esp_err_to_name(err));
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
    OledDefaults defaults = GetOledDefaults();
    char address[3] = {defaults.address[0], defaults.address[1], '\0'};
    int32_t sda = defaults.sda;
    int32_t scl = defaults.scl;
    int32_t width = defaults.width;
    int32_t height = defaults.height;
    if (req->method == HTTP_GET) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("oled_config", NVS_READONLY, &handle);
        if (err == ESP_OK) {
            size_t length = sizeof(address);
            if (nvs_get_str(handle, "addr", address, &length) != ESP_OK ||
                nvs_get_i32(handle, "sda", &sda) != ESP_OK ||
                nvs_get_i32(handle, "scl", &scl) != ESP_OK ||
                nvs_get_i32(handle, "width", &width) != ESP_OK ||
                nvs_get_i32(handle, "height", &height) != ESP_OK) {
                ESP_LOGW(TAG, "Cau hinh OLED trong NVS khong day du");
                std::snprintf(address, sizeof(address), "%s", defaults.address);
                sda = defaults.sda;
                scl = defaults.scl;
                width = defaults.width;
                height = defaults.height;
            }
            nvs_close(handle);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read NVS");
            return ESP_FAIL;
        }
        if (!ValidStoredOledConfig(address, sda, scl, width, height)) {
            ESP_LOGW(TAG, "Cau hinh OLED doc duoc khong hop le, tra ve mac dinh");
            std::snprintf(address, sizeof(address), "%s", defaults.address);
            sda = defaults.sda;
            scl = defaults.scl;
            width = defaults.width;
            height = defaults.height;
        }
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr) {
            cJSON_AddStringToObject(response, "addr", address);
            cJSON_AddNumberToObject(response, "sda", sda);
            cJSON_AddNumberToObject(response, "scl", scl);
            cJSON_AddNumberToObject(response, "width", width);
            cJSON_AddNumberToObject(response, "height", height);
        }
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    int parsed_address = 0;
    int parsed_sda = 0;
    int parsed_scl = 0;
    int parsed_width = 0;
    int parsed_height = 0;
    bool valid = ParseOledAddress(cJSON_GetObjectItem(root, "addr"), &parsed_address) &&
                 GetInteger(root, "sda", 0, SOC_GPIO_PIN_COUNT - 1, &parsed_sda) &&
                 GetInteger(root, "scl", 0, SOC_GPIO_PIN_COUNT - 1, &parsed_scl) &&
                 GetInteger(root, "width", 64, 256, &parsed_width) &&
                 GetInteger(root, "height", 16, 64, &parsed_height) && parsed_width % 8 == 0 &&
                 parsed_height % 8 == 0 && ValidOledPins(parsed_sda, parsed_scl);
    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OLED configuration");
        return ESP_FAIL;
    }
    char normalized_address[3] = {
        static_cast<char>((parsed_address >> 4) < 10 ? '0' + (parsed_address >> 4)
                                                     : 'A' + (parsed_address >> 4) - 10),
        static_cast<char>((parsed_address & 0x0f) < 10 ? '0' + (parsed_address & 0x0f)
                                                       : 'A' + (parsed_address & 0x0f) - 10),
        '\0',
    };

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("oled_config", NVS_READWRITE, &handle);
    bool opened = err == ESP_OK;
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "addr", normalized_address);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "sda", parsed_sda);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "scl", parsed_scl);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "width", parsed_width);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "height", parsed_height);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (opened) {
        nvs_close(handle);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result",
                                "OLED configuration saved. Press Test Display to apply.");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledTestHandler(httpd_req_t* req) {
    ReinitializeOledDisplay();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Display reinitialization scheduled");
        cJSON_AddStringToObject(response, "note", "Check if test pattern is visible on display");
    }
    return SendJson(req, response);
}
