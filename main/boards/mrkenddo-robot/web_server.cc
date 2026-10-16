#include "web_server.h"
#include "config.h"
#include "oled_display_controller.h"
#include "robot_drive.h"
#include "robot_mcp_tools.h"
#include "web_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <esp_http_server.h>
#include <esp_log.h>
#include <application.h>
#include <cJSON.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <settings.h>
#include <soc/soc_caps.h>
#include <wifi_manager.h>

#define TAG "WEB_SERVER"

// Embedded HTML page. The previous inline page is kept below only as a
// reference while the board-local dashboard lives in web_ui.h.
#if 0
static const char* INDEX_HTML_LEGACY =
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
    "            <textarea id=\"prompt\" maxlength=\"1000\" "
    "placeholder=\"Type a command for XiaoZhi...\"></textarea>\n"
    "            <button class=\"btn-primary\" id=\"sendPromptButton\" "
    "onclick=\"sendPrompt()\">Send</button>\n"
    "            <div class=\"response\" id=\"promptResponse\">"
    "Experimental MCP bridge: the server must ask the robot for your message.</div>\n"
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
    "            <div class=\"status\" id=\"status\"></div>\n"
    "        </div>\n"
    "        \n"
    "        <div class=\"section\">\n"
    "            <h2>Motor Configuration</h2>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Move units (duration = units × ms):</label>\n"
    "                <input type=\"number\" id=\"steps\" value=\"512\" min=\"100\" max=\"30000\">\n"
    "            </div>\n"
    "            <div class=\"config-row\">\n"
    "                <label>Milliseconds per unit:</label>\n"
    "                <input type=\"number\" id=\"delay\" value=\"1\" min=\"1\" max=\"100\">\n"
    "            </div>\n"
    "            <div class=\"config-row\"><label>Reverse left motor</label>"
    "<input type=\"checkbox\" id=\"left_inv\"></div>\n"
    "            <div class=\"config-row\"><label>Reverse right motor</label>"
    "<input type=\"checkbox\" id=\"right_inv\" checked></div>\n"
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
    "            <div class=\"config-row\"><label>OLED rotation</label>"
    "<select id=\"oled_rotation\"><option value=\"0\">0°</option>"
    "<option value=\"180\">180°</option></select></div>\n"
    "            <button class=\"btn-save\" onclick=\"saveOledConfig()\">Save OLED "
    "Config</button>\n"
    "            <button class=\"btn-test\" onclick=\"testOled()\">Test Display</button>\n"
    "            <div class=\"status\" id=\"oledStatus\"></div>\n"
    "        </div>\n"
    "        <div class=\"section\"><h2>Main TFT rotation</h2>"
    "<div class=\"config-row\"><label>Rotation</label>"
    "<select id=\"tft_rotation\"><option value=\"0\">0°</option>"
    "<option value=\"90\" selected>90° (recommended)</option>"
    "<option value=\"180\">180°</option><option value=\"270\">270°</option></select></div>"
    "<button class=\"btn-save\" onclick=\"saveTftRotation()\">Apply TFT rotation</button>"
    "<div class=\"status\" id=\"tftStatus\"></div></div>\n"
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
    "                    document.getElementById('left_inv').checked = data.left_inverted;\n"
    "                    document.getElementById('right_inv').checked = data.right_inverted;\n"
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
    "                    document.getElementById('oled_rotation').value = data.rotation;\n"
    "                }\n"
    "            } catch (e) {\n"
    "                console.log('Could not load OLED config, using defaults');\n"
    "            }\n"
    "            try { const res = await fetch('/api/tft_config'); if (res.ok) {"
    " const data = await res.json();"
    " document.getElementById('tft_rotation').value = data.rotation; } }"
    " catch (e) { console.log('Could not load TFT config'); }\n"
    "        }\n"
    "        \n"
    "        loadConfig();\n"
    "        \n"
    "        async function sendPrompt() {\n"
    "            const input = document.getElementById('prompt');\n"
    "            const button = document.getElementById('sendPromptButton');\n"
    "            const output = document.getElementById('promptResponse');\n"
    "            const prompt = input.value.trim();\n"
    "            if (!prompt) return;\n"
    "            button.disabled = true;\n"
    "            output.textContent = 'Queueing message...';\n"
    "            try {\n"
    "                const res = await fetch('/api/prompt', {method: 'POST', "
    "headers: {'Content-Type': 'application/json'}, "
    "body: JSON.stringify({prompt})});\n"
    "                if (!res.ok) throw new Error(await res.text());\n"
    "                output.textContent = 'Waiting for XiaoZhi to request the message...';\n"
    "                for (let i = 0; i < 70; i++) {\n"
    "                    await new Promise(resolve => setTimeout(resolve, 500));\n"
    "                    const check = await fetch('/api/prompt_status');\n"
    "                    if (!check.ok) throw new Error('Cannot read prompt status');\n"
    "                    const state = (await check.json()).status;\n"
    "                    if (state === 'consumed') {\n"
    "                        output.textContent = 'AI requested your message. "
    "Waiting for its reply on the robot.';\n"
    "                        input.value = '';\n"
    "                        return;\n"
    "                    }\n"
    "                    if (state === 'failed' || state === 'timeout') "
    "throw new Error('Server did not request the message (' + state + ')');\n"
    "                }\n"
    "                throw new Error('Server did not request the message within 35 seconds');\n"
    "            } catch (e) {\n"
    "                output.textContent = 'Error: ' + e.message;\n"
    "            } finally {\n"
    "                button.disabled = false;\n"
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
    "                const res = await fetch('/api/move', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ direction: direction, steps: steps, delay_ms: "
    "delay })\n"
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
    "            const leftInverted = document.getElementById('left_inv').checked;\n"
    "            const rightInverted = document.getElementById('right_inv').checked;\n"
    "            status.textContent = 'Saving...';\n"
    "            \n"
    "            try {\n"
    "                const res = await fetch('/api/config', {\n"
    "                    method: 'POST',\n"
    "                    headers: { 'Content-Type': 'application/json' },\n"
    "                    body: JSON.stringify({ steps: parseInt(steps), delay_ms: parseInt(delay), "
    "left_inverted: leftInverted, right_inverted: rightInverted "
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
    "                        height: parseInt(height),\n"
    "                        rotation: parseInt(document.getElementById('oled_rotation').value) \n"
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
    "        async function saveTftRotation() {\n"
    "            const status = document.getElementById('tftStatus');\n"
    "            try { const res = await fetch('/api/tft_config', { method: 'POST',"
    " headers: { 'Content-Type': 'application/json' },"
    " body: JSON.stringify({ rotation: parseInt(document.getElementById('tft_rotation').value) }) "
    "});"
    " status.textContent = res.ok ? (await res.json()).result : await res.text(); }"
    " catch (e) { status.textContent = 'Error: ' + e.message; }\n"
    "        }\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";
#endif

static const char* INDEX_HTML = mrkenddo::web_ui::kIndexHtml;

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

bool GetString(const cJSON* object, const char* name, char* value, size_t value_size,
               bool required = true) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) {
        return !required;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr ||
        std::strlen(item->valuestring) >= value_size) {
        return false;
    }
    std::snprintf(value, value_size, "%s", item->valuestring);
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

WebServer::WebServer(RobotDrive* drive) : server_(nullptr), drive_(drive) {}

WebServer::~WebServer() { Stop(); }

bool WebServer::Start(int port) {
    if (port < 1 || port > 65535 || server_ != nullptr) {
        ESP_LOGE(TAG, "Cong HTTP khong hop le hoac server da chay: %d", port);
        return false;
    }

    httpd_uri_t uris[] = {
        {"/", HTTP_GET, RootHandler, this},
        {"/api/notify", HTTP_POST, ApiNotifyHandler, this},
        {"/api/notify/stop", HTTP_POST, ApiNotifyStopHandler, this},
        {"/api/radio/start", HTTP_POST, ApiRadioStartHandler, this},
        {"/api/radio/stop", HTTP_POST, ApiRadioStopHandler, this},
        {"/api/radio/status", HTTP_GET, ApiRadioStatusHandler, this},
        {"/api/prompt", HTTP_POST, ApiPromptHandler, this},
        {"/api/prompt_status", HTTP_GET, ApiPromptStatusHandler, this},
        {"/api/move", HTTP_POST, ApiMoveHandler, this},
        {"/api/stop", HTTP_POST, ApiStopHandler, this},
        {"/api/routine", HTTP_POST, ApiRoutineHandler, this},
        {"/api/motion_status", HTTP_GET, ApiMotionStatusHandler, this},
        {"/api/config", HTTP_POST, ApiConfigHandler, this},
        {"/api/config", HTTP_GET, ApiConfigHandler, this},
        {"/api/oled_config", HTTP_POST, ApiOledConfigHandler, this},
        {"/api/oled_config", HTTP_GET, ApiOledConfigHandler, this},
        {"/api/oled_test", HTTP_POST, ApiOledTestHandler, this},
        {"/api/oled_message", HTTP_POST, ApiOledMessageHandler, this},
        {"/api/oled_clear", HTTP_POST, ApiOledClearHandler, this},
        {"/api/status", HTTP_GET, ApiStatusHandler, this},
        {"/api/tft_config", HTTP_GET, ApiTftConfigHandler, this},
        {"/api/tft_config", HTTP_POST, ApiTftConfigHandler, this},
        {"/api/boot_config", HTTP_GET, ApiBootConfigHandler, this},
        {"/api/boot_config", HTTP_POST, ApiBootConfigHandler, this},
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;
    config.max_uri_handlers = sizeof(uris) / sizeof(uris[0]);

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong the khoi dong HTTP server: %s", esp_err_to_name(ret));
        return false;
    }

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

esp_err_t WebServer::ApiNotifyHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }

    char audio_url[768] = {};
    bool valid = GetString(root, "audio_url", audio_url, sizeof(audio_url));
    valid = valid && (std::strncmp(audio_url, "http://", 7) == 0 ||
                      std::strncmp(audio_url, "https://", 8) == 0);

    std::vector<NotifySubtitle> subtitles;
    const cJSON* subtitles_json = cJSON_GetObjectItemCaseSensitive(root, "subtitles");
    if (valid && subtitles_json != nullptr) {
        if (!cJSON_IsArray(subtitles_json) || cJSON_GetArraySize(subtitles_json) > 32) {
            valid = false;
        } else {
            const cJSON* item = nullptr;
            cJSON_ArrayForEach (item, subtitles_json) {
                int start_ms = 0;
                char text[256] = {};
                if (!cJSON_IsObject(item) ||
                    !GetInteger(item, "start_ms", 0, std::numeric_limits<int>::max(), &start_ms) ||
                    !GetString(item, "text", text, sizeof(text))) {
                    valid = false;
                    break;
                }
                subtitles.push_back(
                    NotifySubtitle{static_cast<uint32_t>(start_ms), std::string(text)});
            }
        }
    }

    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid notify payload");
        return ESP_FAIL;
    }

    Application::GetInstance().RequestNotification(std::string(audio_url), std::move(subtitles));
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiNotifyStopHandler(httpd_req_t* req) {
    Application::GetInstance().RequestStopNotification();
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "stopping");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStartHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }

    char audio_url[768] = {};
    bool valid = GetString(root, "audio_url", audio_url, sizeof(audio_url));
    valid = valid && (std::strncmp(audio_url, "http://", 7) == 0 ||
                      std::strncmp(audio_url, "https://", 8) == 0);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid radio payload");
        return ESP_FAIL;
    }

    Application::GetInstance().RequestRadio(std::string(audio_url));
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStopHandler(httpd_req_t* req) {
    Application::GetInstance().RequestStopRadio();
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "stopping");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRadioStatusHandler(httpd_req_t* req) {
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        const bool active = Application::GetInstance().IsRadioActive();
        cJSON_AddBoolToObject(response, "active", active);
        cJSON_AddStringToObject(response, "state", active ? "radio" : "idle");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiPromptHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    const cJSON* prompt = cJSON_GetObjectItem(root, "prompt");
    if (!cJSON_IsString(prompt) || prompt->valuestring == nullptr ||
        prompt->valuestring[0] == '\0' || std::strlen(prompt->valuestring) > 1000) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid prompt");
        return ESP_FAIL;
    }
    bool queued = QueueRobotWebPrompt(prompt->valuestring);
    cJSON_Delete(root);
    if (!queued) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "A prompt is already pending or the robot is busy",
                               HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "202 Accepted");
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", "queued");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiPromptStatusHandler(httpd_req_t* req) {
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "status", GetRobotWebPromptStatus());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMoveHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->drive_ == nullptr) {
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
    bool valid = GetInteger(root, "steps", -RobotDrive::kMaxSteps, RobotDrive::kMaxSteps, &steps) &&
                 GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                            &delay_ms, false);
    // Keep the historical optional delay_ms API, but reject a malformed value
    // when the field is present.
    if (valid && cJSON_GetObjectItem(root, "delay_ms") != nullptr) {
        valid = GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                           &delay_ms);
    }
    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    const cJSON* direction_item = cJSON_GetObjectItemCaseSensitive(root, "direction");
    RobotDrive::Direction direction =
        steps < 0 ? RobotDrive::Direction::Backward : RobotDrive::Direction::Forward;
    if (direction_item != nullptr) {
        if (!cJSON_IsString(direction_item) || direction_item->valuestring == nullptr ||
            steps <= 0) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction or steps");
            return ESP_FAIL;
        }
        const char* value = direction_item->valuestring;
        if (std::strcmp(value, "forward") == 0)
            direction = RobotDrive::Direction::Forward;
        else if (std::strcmp(value, "backward") == 0)
            direction = RobotDrive::Direction::Backward;
        else if (std::strcmp(value, "left") == 0)
            direction = RobotDrive::Direction::Left;
        else if (std::strcmp(value, "right") == 0)
            direction = RobotDrive::Direction::Right;
        else {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid direction");
            return ESP_FAIL;
        }
    }
    bool started = direction_item == nullptr ? server->drive_->Step(steps, delay_ms)
                                             : server->drive_->Move(direction, steps, delay_ms);
    cJSON_Delete(root);
    if (!started) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Motor command failed");
        return ESP_FAIL;
    }
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Moving");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiStopHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->drive_ == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    server->drive_->Stop();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Stopped");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiRoutineHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    if (server == nullptr || server->drive_ == nullptr) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Motor unavailable", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    const cJSON* routine = cJSON_GetObjectItemCaseSensitive(root, "routine");
    if (!cJSON_IsString(routine) || routine->valuestring == nullptr) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid routine");
        return ESP_FAIL;
    }

    RobotDrive::Routine routine_type;
    if (std::strcmp(routine->valuestring, "lively") == 0) {
        routine_type = RobotDrive::Routine::Lively;
    } else if (std::strcmp(routine->valuestring, "dance") == 0) {
        routine_type = RobotDrive::Routine::Dance;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Routine must be lively or dance");
        return ESP_FAIL;
    }

    const bool started = server->drive_->StartRoutine(routine_type);
    cJSON_Delete(root);
    if (!started) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Robot is already moving", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Routine started");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiMotionStatusHandler(httpd_req_t* req) {
    WebServer* server = GetServer(req);
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(
            response, "routine_running",
            server != nullptr && server->drive_ != nullptr && server->drive_->IsRoutineRunning());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiConfigHandler(httpd_req_t* req) {
    int32_t steps = kDefaultMotorSteps;
    int32_t delay_ms = kDefaultMotorDelayMs;
    uint8_t left_inv = 0;
    uint8_t right_inv = 1;
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
            if (nvs_get_u8(handle, "left_inv", &left_inv) != ESP_OK || left_inv > 1)
                left_inv = 0;
            if (nvs_get_u8(handle, "right_inv", &right_inv) != ESP_OK || right_inv > 1)
                right_inv = 1;
            nvs_close(handle);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Khong the mo NVS dong co: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read NVS");
            return ESP_FAIL;
        }
        steps = std::clamp<int32_t>(steps, 1, RobotDrive::kMaxSteps);
        delay_ms = std::clamp<int32_t>(delay_ms, RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs);
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr) {
            cJSON_AddNumberToObject(response, "steps", steps);
            cJSON_AddNumberToObject(response, "delay_ms", delay_ms);
            cJSON_AddBoolToObject(response, "left_inverted", left_inv != 0);
            cJSON_AddBoolToObject(response, "right_inverted", right_inv != 0);
        }
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    int saved_steps = 0;
    int saved_delay_ms = 0;
    if (!GetInteger(root, "steps", 1, RobotDrive::kMaxSteps, &saved_steps) ||
        !GetInteger(root, "delay_ms", RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs,
                    &saved_delay_ms)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid steps or delay_ms");
        return ESP_FAIL;
    }
    const cJSON* left_item = cJSON_GetObjectItemCaseSensitive(root, "left_inverted");
    const cJSON* right_item = cJSON_GetObjectItemCaseSensitive(root, "right_inverted");
    if ((left_item != nullptr && !cJSON_IsBool(left_item)) ||
        (right_item != nullptr && !cJSON_IsBool(right_item))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid motor inversion");
        return ESP_FAIL;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("motor_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    // Older clients may omit polarity flags. Keep the saved polarity in that case.
    if (left_item == nullptr) {
        if (nvs_get_u8(handle, "left_inv", &left_inv) != ESP_OK || left_inv > 1)
            left_inv = 0;
    } else {
        left_inv = cJSON_IsTrue(left_item);
    }
    if (right_item == nullptr) {
        if (nvs_get_u8(handle, "right_inv", &right_inv) != ESP_OK || right_inv > 1)
            right_inv = 1;
    } else {
        right_inv = cJSON_IsTrue(right_item);
    }
    cJSON_Delete(root);
    err = nvs_set_i32(handle, "steps", saved_steps);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "delay_ms", saved_delay_ms);
    }
    if (err == ESP_OK)
        err = nvs_set_u8(handle, "left_inv", left_inv);
    if (err == ESP_OK)
        err = nvs_set_u8(handle, "right_inv", right_inv);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the luu cau hinh dong co: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }
    WebServer* server = GetServer(req);
    if (server != nullptr && server->drive_ != nullptr) {
        server->drive_->SetInversion(left_inv != 0, right_inv != 0);
        server->drive_->SetSpeed(saved_delay_ms);
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
    int32_t rotation = 0;
    int32_t mode = static_cast<int32_t>(mrkenddo::OledDisplayMode::Clock);
    char text[sizeof(mrkenddo::OledDisplaySettings::text)] = {};
    int32_t speed = 2;
    int32_t font_size = 0;
    int32_t contrast = 255;
    bool invert = false;
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
            if (nvs_get_i32(handle, "rotation", &rotation) != ESP_OK ||
                (rotation != 0 && rotation != 180))
                rotation = 0;
            if (nvs_get_i32(handle, "mode", &mode) != ESP_OK ||
                !mrkenddo::OledDisplayController::IsValidMode(mode))
                mode = static_cast<int32_t>(mrkenddo::OledDisplayMode::Clock);
            size_t text_length = sizeof(text);
            if (nvs_get_str(handle, "text", text, &text_length) != ESP_OK)
                text[0] = '\0';
            if (nvs_get_i32(handle, "speed", &speed) != ESP_OK ||
                !mrkenddo::OledDisplayController::IsValidSpeed(speed))
                speed = 2;
            if (nvs_get_i32(handle, "font_size", &font_size) != ESP_OK ||
                !mrkenddo::OledDisplayController::IsValidFontSize(font_size))
                font_size = 0;
            if (nvs_get_i32(handle, "contrast", &contrast) != ESP_OK || contrast < 0 ||
                contrast > 255)
                contrast = 255;
            uint8_t invert_value = 0;
            if (nvs_get_u8(handle, "invert", &invert_value) == ESP_OK && invert_value <= 1)
                invert = invert_value != 0;
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
            cJSON_AddNumberToObject(response, "rotation", rotation);
            cJSON_AddNumberToObject(response, "mode", mode);
            cJSON_AddStringToObject(response, "text", text);
            cJSON_AddNumberToObject(response, "speed", speed);
            cJSON_AddNumberToObject(response, "font_size", font_size);
            cJSON_AddNumberToObject(response, "contrast", contrast);
            cJSON_AddBoolToObject(response, "invert", invert);
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
    int parsed_rotation = 0;
    int parsed_mode = static_cast<int>(mrkenddo::OledDisplayMode::Clock);
    int parsed_speed = 2;
    int parsed_font_size = 0;
    int parsed_contrast = 255;
    char parsed_text[sizeof(text)] = {};
    bool parsed_invert = false;
    bool valid = ParseOledAddress(cJSON_GetObjectItem(root, "addr"), &parsed_address) &&
                 GetInteger(root, "sda", 0, SOC_GPIO_PIN_COUNT - 1, &parsed_sda) &&
                 GetInteger(root, "scl", 0, SOC_GPIO_PIN_COUNT - 1, &parsed_scl) &&
                 GetInteger(root, "width", 64, 256, &parsed_width) &&
                 GetInteger(root, "height", 16, 64, &parsed_height) && parsed_width % 8 == 0 &&
                 parsed_height % 8 == 0 && ValidOledPins(parsed_sda, parsed_scl) &&
                 GetInteger(root, "rotation", 0, 180, &parsed_rotation, false) &&
                 (parsed_rotation == 0 || parsed_rotation == 180) &&
                 GetInteger(root, "mode", 0, 3, &parsed_mode, false) &&
                 GetString(root, "text", parsed_text, sizeof(parsed_text), false) &&
                 GetInteger(root, "speed", 1, 5, &parsed_speed, false) &&
                 GetInteger(root, "font_size", 0, 2, &parsed_font_size, false) &&
                 GetInteger(root, "contrast", 0, 255, &parsed_contrast, false);
    const cJSON* invert_item = cJSON_GetObjectItemCaseSensitive(root, "invert");
    if (valid && invert_item != nullptr) {
        valid = cJSON_IsBool(invert_item);
        parsed_invert = cJSON_IsTrue(invert_item);
    }
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
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "rotation", parsed_rotation);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "mode", parsed_mode);
    if (err == ESP_OK)
        err = nvs_set_str(handle, "text", parsed_text);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "speed", parsed_speed);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "font_size", parsed_font_size);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "contrast", parsed_contrast);
    if (err == ESP_OK)
        err = nvs_set_u8(handle, "invert", parsed_invert ? 1 : 0);
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
        cJSON_AddStringToObject(response, "result", "OLED configuration saved and applying");
    }
    ReinitializeOledDisplay();
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledTestHandler(httpd_req_t* req) {
    ReinitializeOledDisplay();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddStringToObject(response, "result", "Display reinitialization scheduled");
        cJSON_AddStringToObject(response, "note", "Check if the clock is visible on display");
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledMessageHandler(httpd_req_t* req) {
    cJSON* root = ParseBody(req);
    if (root == nullptr) {
        return ESP_FAIL;
    }
    char text[sizeof(mrkenddo::OledDisplaySettings::text)] = {};
    int speed = 2;
    int font_size = 0;
    bool valid = GetString(root, "text", text, sizeof(text)) &&
                 GetInteger(root, "speed", 1, 5, &speed, false) &&
                 GetInteger(root, "font_size", 0, 2, &font_size, false);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OLED message");
        return ESP_FAIL;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("oled_config", NVS_READWRITE, &handle);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "mode", static_cast<int32_t>(mrkenddo::OledDisplayMode::Marquee));
    if (err == ESP_OK)
        err = nvs_set_str(handle, "text", text);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "speed", speed);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "font_size", font_size);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    if (handle != 0)
        nvs_close(handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save OLED message");
        return ESP_FAIL;
    }
    ApplyOledDisplaySettings();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "OLED marquee scheduled");
    return SendJson(req, response);
}

esp_err_t WebServer::ApiOledClearHandler(httpd_req_t* req) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("oled_config", NVS_READWRITE, &handle);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "mode", static_cast<int32_t>(mrkenddo::OledDisplayMode::Off));
    if (err == ESP_OK)
        err = nvs_commit(handle);
    if (handle != 0)
        nvs_close(handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to clear OLED");
        return ESP_FAIL;
    }
    ApplyOledDisplaySettings();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "OLED cleared");
    return SendJson(req, response);
}

const char* DeviceStateName(DeviceState state) {
    switch (state) {
        case kDeviceStateStarting:
            return "starting";
        case kDeviceStateWifiConfiguring:
            return "wifi_configuring";
        case kDeviceStateIdle:
            return "idle";
        case kDeviceStateConnecting:
            return "connecting";
        case kDeviceStateListening:
            return "listening";
        case kDeviceStateSpeaking:
            return "speaking";
        case kDeviceStateNotifying:
            return "notifying";
        case kDeviceStateRadio:
            return "radio";
        case kDeviceStateUpgrading:
            return "upgrading";
        case kDeviceStateActivating:
            return "activating";
        case kDeviceStateAudioTesting:
            return "audio_testing";
        case kDeviceStateFatalError:
            return "fatal_error";
        case kDeviceStateUnknown:
        default:
            return "unknown";
    }
}

esp_err_t WebServer::ApiStatusHandler(httpd_req_t* req) {
    const auto& wifi = WifiManager::GetInstance();
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(response, "wifi_connected", wifi.IsConnected());
        cJSON_AddStringToObject(response, "ip",
                                wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
        cJSON_AddStringToObject(response, "state",
                                DeviceStateName(Application::GetInstance().GetDeviceState()));
        cJSON_AddNumberToObject(response, "free_heap", esp_get_free_heap_size());
    }
    return SendJson(req, response);
}

esp_err_t WebServer::ApiTftConfigHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        int32_t rotation = DISPLAY_DEFAULT_ROTATION;
        nvs_handle_t handle;
        esp_err_t err = nvs_open("tft_config", NVS_READONLY, &handle);
        if (err == ESP_OK) {
            if (nvs_get_i32(handle, "rotation", &rotation) != ESP_OK ||
                (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
                rotation = DISPLAY_DEFAULT_ROTATION;
            }
            nvs_close(handle);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read NVS");
            return ESP_FAIL;
        }
        cJSON* response = cJSON_CreateObject();
        if (response != nullptr)
            cJSON_AddNumberToObject(response, "rotation", rotation);
        return SendJson(req, response);
    }

    cJSON* root = ParseBody(req);
    if (root == nullptr)
        return ESP_FAIL;
    int rotation = 0;
    bool valid = GetInteger(root, "rotation", 0, 270, &rotation) && rotation % 90 == 0;
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid TFT rotation");
        return ESP_FAIL;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tft_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        return ESP_FAIL;
    }
    err = nvs_set_i32(handle, "rotation", rotation);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save NVS");
        return ESP_FAIL;
    }
    ApplyTftRotation(rotation);
    cJSON* response = cJSON_CreateObject();
    if (response != nullptr)
        cJSON_AddStringToObject(response, "result", "TFT rotation scheduled");
    return SendJson(req, response);
}

esp_err_t WebServer::ApiBootConfigHandler(httpd_req_t* req) {
    Settings settings("boot_sequence", req->method == HTTP_POST);
    bool enabled = settings.GetBool("enabled", true);
    int volume = std::clamp<int32_t>(settings.GetInt("volume", 45), 0, 100);

    if (req->method == HTTP_POST) {
        cJSON* root = ParseBody(req);
        if (root == nullptr) {
            return ESP_FAIL;
        }

        cJSON* enabled_json = cJSON_GetObjectItem(root, "enabled");
        int parsed_volume = volume;
        bool valid =
            cJSON_IsBool(enabled_json) && GetInteger(root, "volume", 0, 100, &parsed_volume);
        if (valid) {
            enabled = cJSON_IsTrue(enabled_json);
            volume = parsed_volume;
            settings.SetBool("enabled", enabled);
            settings.SetInt("volume", volume);
            settings.SetInt("version", 2);
        }
        cJSON_Delete(root);
        if (!valid) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid boot audio configuration");
            return ESP_FAIL;
        }
        Application::GetInstance().SetBootAudioConfig(enabled, volume);
    }

    cJSON* response = cJSON_CreateObject();
    if (response != nullptr) {
        cJSON_AddBoolToObject(response, "enabled", enabled);
        cJSON_AddNumberToObject(response, "volume", volume);
        if (req->method == HTTP_POST) {
            cJSON_AddStringToObject(response, "result", "Boot sequence configuration saved");
        }
    }
    return SendJson(req, response);
}
