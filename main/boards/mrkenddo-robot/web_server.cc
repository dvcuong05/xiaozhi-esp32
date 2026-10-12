#include "web_server.h"
#include "application.h"
#include "stepper_motor.h"
#include "robot_mcp_tools.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <nvs.h>

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
"        body { font-family: Arial, sans-serif; max-width: 600px; margin: 0 auto; padding: 20px; background: #f5f5f5; }\n"
"        .container { background: white; border-radius: 10px; padding: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n"
"        h1 { color: #333; margin-bottom: 20px; text-align: center; }\n"
"        .section { margin-bottom: 20px; }\n"
"        .section h2 { color: #666; font-size: 16px; margin-bottom: 10px; }\n"
"        textarea { width: 100%; height: 80px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; resize: vertical; }\n"
"        button { padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 14px; margin: 5px; }\n"
"        .btn-primary { background: #007bff; color: white; }\n"
"        .btn-danger { background: #dc3545; color: white; }\n"
"        .btn-control { background: #28a745; color: white; width: 80px; height: 80px; font-size: 24px; margin: 5px; }\n"
"        .controls { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; max-width: 300px; margin: 0 auto; }\n"
"        .controls .empty { visibility: hidden; }\n"
"        .response { background: #f8f9fa; padding: 10px; border-radius: 5px; margin-top: 10px; min-height: 50px; }\n"
"        .status { color: #666; font-size: 12px; margin-top: 5px; }\n"
"        .config-row { display: flex; align-items: center; margin-bottom: 10px; }\n"
"        .config-row label { flex: 1; color: #666; }\n"
"        .config-row input { width: 80px; padding: 5px; border: 1px solid #ddd; border-radius: 5px; }\n"
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
"                <input type=\"number\" id=\"oled_width\" value=\"128\" min=\"64\" max=\"256\">\n"
"            </div>\n"
"            <div class=\"config-row\">\n"
"                <label>Height:</label>\n"
"                <input type=\"number\" id=\"oled_height\" value=\"32\" min=\"16\" max=\"64\">\n"
"            </div>\n"
"            <button class=\"btn-save\" onclick=\"saveOledConfig()\">Save OLED Config</button>\n"
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
"                    if (data.delay_ms) document.getElementById('delay').value = data.delay_ms;\n"
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
"                    if (data.width) document.getElementById('oled_width').value = data.width;\n"
"                    if (data.height) document.getElementById('oled_height').value = data.height;\n"
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
"                    body: JSON.stringify({ steps: parseInt(steps), delay_ms: parseInt(delay) })\n"
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

static StepperMotor* g_web_stepper_motor = nullptr;

WebServer::WebServer(StepperMotor* stepper_motor) : server_(nullptr), stepper_motor_(stepper_motor) {
    g_web_stepper_motor = stepper_motor;
}

WebServer::~WebServer() {
    Stop();
}

bool WebServer::Start(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;
    
    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong the khoi dong HTTP server: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Register root handler
    httpd_uri_t uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = RootHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &uri);
    
    // Register API handlers
    httpd_uri_t api_prompt = {
        .uri = "/api/prompt",
        .method = HTTP_POST,
        .handler = ApiPromptHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_prompt);
    
    httpd_uri_t api_move = {
        .uri = "/api/move",
        .method = HTTP_POST,
        .handler = ApiMoveHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_move);
    
    httpd_uri_t api_stop = {
        .uri = "/api/stop",
        .method = HTTP_POST,
        .handler = ApiStopHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_stop);
    
    httpd_uri_t api_config = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = ApiConfigHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_config);
    
    httpd_uri_t api_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = ApiConfigHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_config_get);
    
    httpd_uri_t api_oled_config = {
        .uri = "/api/oled_config",
        .method = HTTP_POST,
        .handler = ApiOledConfigHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_oled_config);
    
    httpd_uri_t api_oled_config_get = {
        .uri = "/api/oled_config",
        .method = HTTP_GET,
        .handler = ApiOledConfigHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_oled_config_get);
    
    httpd_uri_t api_oled_test = {
        .uri = "/api/oled_test",
        .method = HTTP_POST,
        .handler = ApiOledTestHandler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &api_oled_test);
    
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
    httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    return ESP_OK;
}

esp_err_t WebServer::ApiPromptHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "ApiPromptHandler duoc goi");
    char content[1024];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Khong the nhan du lieu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Nhan prompt: %s", content);
    
    cJSON* root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE(TAG, "JSON khong hop le");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON* prompt_json = cJSON_GetObjectItem(root, "prompt");
    if (!prompt_json || !cJSON_IsString(prompt_json)) {
        ESP_LOGE(TAG, "Thieu tham so prompt");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing prompt");
        return ESP_FAIL;
    }
    
    const char* prompt = cJSON_GetStringValue(prompt_json);
    ESP_LOGI(TAG, "Gui prompt qua MCP: %s", prompt);
    
    // Create proper JSON message for the prompt (matching robot_mcp_tools pattern)
    cJSON* message_json = cJSON_CreateObject();
    if (!message_json) {
        ESP_LOGE(TAG, "Khong the tao JSON message");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(message_json, "type", "text");
    cJSON_AddStringToObject(message_json, "text", prompt);
    
    char* message_str = cJSON_PrintUnformatted(message_json);
    if (!message_str) {
        ESP_LOGE(TAG, "Khong the serialize JSON");
        cJSON_Delete(message_json);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON");
        return ESP_FAIL;
    }
    
    // Send prompt to application via MCP
    auto& app = Application::GetInstance();
    app.SendMcpMessage(std::string(message_str));
    
    cJSON_free(message_str);
    cJSON_Delete(message_json);
    cJSON_Delete(root);
    
    // Send response
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "Prompt sent");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t WebServer::ApiMoveHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "ApiMoveHandler duoc goi");
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Khong the nhan du lieu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Nhan du lieu: %s", content);
    
    cJSON* root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE(TAG, "JSON khong hop le");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON* steps_json = cJSON_GetObjectItem(root, "steps");
    cJSON* delay_json = cJSON_GetObjectItem(root, "delay_ms");
    
    if (!steps_json || !cJSON_IsNumber(steps_json)) {
        ESP_LOGE(TAG, "Thieu tham so steps");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing steps");
        return ESP_FAIL;
    }
    
    int steps = cJSON_GetNumberValue(steps_json);
    int delay_ms = delay_json && cJSON_IsNumber(delay_json) ? cJSON_GetNumberValue(delay_json) : 2;
    ESP_LOGI(TAG, "Dieu khien dong co: steps=%d, delay_ms=%d", steps, delay_ms);
    
    if (g_web_stepper_motor) {
        ESP_LOGI(TAG, "Thuc thi Step()");
        g_web_stepper_motor->Step(steps, delay_ms);
        ESP_LOGI(TAG, "Step() hoan tat");
    } else {
        ESP_LOGE(TAG, "g_web_stepper_motor la NULL!");
    }
    
    cJSON_Delete(root);
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "Moving");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t WebServer::ApiStopHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "ApiStopHandler duoc goi");
    if (g_web_stepper_motor) {
        ESP_LOGI(TAG, "Thuc thi Stop()");
        g_web_stepper_motor->Stop();
        ESP_LOGI(TAG, "Stop() hoan tat");
    } else {
        ESP_LOGE(TAG, "g_web_stepper_motor la NULL!");
    }
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "Stopped");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t WebServer::ApiConfigHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // Load configuration from NVS
        ESP_LOGI(TAG, "ApiConfigHandler GET - tai cau hinh tu NVS");
        
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("motor_config", NVS_READONLY, &nvs_handle);
        int32_t steps = 512;
        int32_t delay_ms = 1;
        
        if (err == ESP_OK) {
            nvs_get_i32(nvs_handle, "steps", &steps);
            nvs_get_i32(nvs_handle, "delay_ms", &delay_ms);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "Da tai cau hinh: steps=%d, delay_ms=%d", steps, delay_ms);
        } else {
            ESP_LOGW(TAG, "Khong tim thay cau hinh, su dung mac dinh");
        }
        
        cJSON* response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "steps", steps);
        cJSON_AddNumberToObject(response, "delay_ms", delay_ms);
        char* response_str = cJSON_PrintUnformatted(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response_str, strlen(response_str));
        cJSON_free(response_str);
        cJSON_Delete(response);
        
        return ESP_OK;
    }
    
    // POST - Save configuration to NVS
    ESP_LOGI(TAG, "ApiConfigHandler POST - luu cau hinh vao NVS");
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Khong the nhan du lieu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Nhan cau hinh: %s", content);
    
    cJSON* root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE(TAG, "JSON khong hop le");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON* steps_json = cJSON_GetObjectItem(root, "steps");
    cJSON* delay_json = cJSON_GetObjectItem(root, "delay_ms");
    
    if (!steps_json || !cJSON_IsNumber(steps_json) || !delay_json || !cJSON_IsNumber(delay_json)) {
        ESP_LOGE(TAG, "Thieu tham so steps hoac delay_ms");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
        return ESP_FAIL;
    }
    
    int steps = cJSON_GetNumberValue(steps_json);
    int delay_ms = cJSON_GetNumberValue(delay_json);
    ESP_LOGI(TAG, "Luu cau hinh: steps=%d, delay_ms=%d", steps, delay_ms);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("motor_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo NVS: %s", esp_err_to_name(err));
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        return ESP_FAIL;
    }
    
    nvs_set_i32(nvs_handle, "steps", steps);
    nvs_set_i32(nvs_handle, "delay_ms", delay_ms);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Da luu cau hinh vao NVS");
    cJSON_Delete(root);
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "Configuration saved");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t WebServer::ApiOledConfigHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // Load OLED configuration from NVS
        ESP_LOGI(TAG, "ApiOledConfigHandler GET - tai cau hinh OLED tu NVS");
        
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("oled_config", NVS_READONLY, &nvs_handle);
        char addr[8] = "3C";
        int32_t sda = 2;
        int32_t scl = 1;
        int32_t width = 128;
        int32_t height = 32;
        
        if (err == ESP_OK) {
            size_t len = sizeof(addr);
            nvs_get_str(nvs_handle, "addr", addr, &len);
            nvs_get_i32(nvs_handle, "sda", &sda);
            nvs_get_i32(nvs_handle, "scl", &scl);
            nvs_get_i32(nvs_handle, "width", &width);
            nvs_get_i32(nvs_handle, "height", &height);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "Da tai cau hinh OLED: addr=%s, sda=%d, scl=%d, width=%d, height=%d", addr, sda, scl, width, height);
        } else {
            ESP_LOGW(TAG, "Khong tim thay cau hinh OLED, su dung mac dinh");
        }
        
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "addr", addr);
        cJSON_AddNumberToObject(response, "sda", sda);
        cJSON_AddNumberToObject(response, "scl", scl);
        cJSON_AddNumberToObject(response, "width", width);
        cJSON_AddNumberToObject(response, "height", height);
        char* response_str = cJSON_PrintUnformatted(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response_str, strlen(response_str));
        cJSON_free(response_str);
        cJSON_Delete(response);
        
        return ESP_OK;
    }
    
    // POST - Save OLED configuration to NVS
    ESP_LOGI(TAG, "ApiOledConfigHandler POST - luu cau hinh OLED vao NVS");
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Khong the nhan du lieu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Nhan cau hinh OLED: %s", content);
    
    cJSON* root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE(TAG, "JSON khong hop le");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON* addr_json = cJSON_GetObjectItem(root, "addr");
    cJSON* sda_json = cJSON_GetObjectItem(root, "sda");
    cJSON* scl_json = cJSON_GetObjectItem(root, "scl");
    cJSON* width_json = cJSON_GetObjectItem(root, "width");
    cJSON* height_json = cJSON_GetObjectItem(root, "height");
    
    if (!addr_json || !cJSON_IsString(addr_json) || !sda_json || !cJSON_IsNumber(sda_json) ||
        !scl_json || !cJSON_IsNumber(scl_json) || !width_json || !cJSON_IsNumber(width_json) ||
        !height_json || !cJSON_IsNumber(height_json)) {
        ESP_LOGE(TAG, "Thieu tham so OLED config");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
        return ESP_FAIL;
    }
    
    const char* addr = cJSON_GetStringValue(addr_json);
    int sda = cJSON_GetNumberValue(sda_json);
    int scl = cJSON_GetNumberValue(scl_json);
    int width = cJSON_GetNumberValue(width_json);
    int height = cJSON_GetNumberValue(height_json);
    ESP_LOGI(TAG, "Luu cau hinh OLED: addr=%s, sda=%d, scl=%d, width=%d, height=%d", addr, sda, scl, width, height);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("oled_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo NVS: %s", esp_err_to_name(err));
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
        return ESP_FAIL;
    }
    
    nvs_set_str(nvs_handle, "addr", addr);
    nvs_set_i32(nvs_handle, "sda", sda);
    nvs_set_i32(nvs_handle, "scl", scl);
    nvs_set_i32(nvs_handle, "width", width);
    nvs_set_i32(nvs_handle, "height", height);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Da luu cau hinh OLED vao NVS");
    cJSON_Delete(root);
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "OLED configuration saved. Reboot to apply changes.");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t WebServer::ApiOledTestHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "ApiOledTestHandler duoc goi");
    
    // Declare external function to reinitialize OLED
    extern void ReinitializeOledDisplay();
    
    // Call the reinitialization function
    ReinitializeOledDisplay();
    
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", "Display reinitialized with test pattern");
    cJSON_AddStringToObject(response, "note", "Check if test pattern is visible on display");
    char* response_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    cJSON_free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}
