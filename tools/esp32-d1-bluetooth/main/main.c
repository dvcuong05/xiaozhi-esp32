// Standalone ESP32 A2DP Source bring-up. No robot or I2S wiring is needed.
// API sequence follows Espressif's ESP-IDF 6.1 a2dp_source example.
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_bt_api.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SAMPLE_RATE 44100U
#define TONE_FRAMES (6U * SAMPLE_RATE)
#define NAME_SIZE 65
#define PEER_COUNT 16
#define AP_SSID "KyNangSo-Robot BT"
static const char* TAG = "ESP32_D1_BT";

typedef enum {
    EV_FOUND,
    EV_DISCOVERY,
    EV_PROFILE,
    EV_CONNECTION,
    EV_AUDIO,
    EV_ACK,
    EV_CODEC_CAPS,
    EV_CODEC_SET,
    EV_COMMAND,
    EV_WEB_SCAN,
    EV_WEB_CONNECT,
    EV_WEB_DISCONNECT
} event_kind_t;
typedef struct {
    event_kind_t kind;
    int value;
    int detail;
    esp_bd_addr_t address;
    esp_a2d_mcc_t codec;
    char text[96];
} event_t;
typedef struct {
    esp_bd_addr_t address;
    char name[64];
    int rssi;
    int64_t last_seen;
} peer_t;

static QueueHandle_t events;
static char target[NAME_SIZE] = "";
static char active_target[NAME_SIZE] = "";
static char state_message[128] = "Đang khởi động.";
static char legacy_pin[5] = "0000";
static esp_bd_addr_t peer;
static volatile bool ready, discovering, found, connecting, connected, streaming;
static volatile bool switch_pending, switch_waiting_suspend, switch_disconnect_requested;
static bool switch_connecting, scan_after_disconnect;
static esp_bd_addr_t pending_peer;
static char pending_name[NAME_SIZE] = "";
static unsigned switch_disconnect_attempts;
static unsigned scan_disconnect_attempts;
static int64_t switch_deadline;
static int64_t scan_disconnect_deadline;
static bool codec_ready;
static bool auto_tone_played, tone_report_pending;
static int64_t next_action;
static peer_t peers[PEER_COUNT];
static portMUX_TYPE peers_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE audio_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t tone_cursor = TONE_FRAMES;
static uint32_t pcm_frames;
static int16_t sine_table[256];
static const char PAGE[] =
    "<!doctype html><html lang='vi'><meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<meta charset='utf-8'><title>ESP32 D1 Bluetooth</title><style>body{font:16px "
    "system-ui;max-width:680px;"
    "margin:24px auto;padding:0 "
    "16px;background:#f3f6fb;color:#172b45}button,select{font:inherit;padding:10px;"
    "margin:5px;border-radius:8px}button{cursor:pointer}.card{background:white;padding:16px;margin:"
    "12px 0;"
    "border-radius:12px}#status{white-space:pre-wrap}</style><h1>Loa Bluetooth</h1>"
    "<div class=card><b>1. Chọn loa</b><br><button onclick='scan()'>Quét thiết bị "
    "Bluetooth</button>"
    "<select id=devices style='max-width:100%'></select><button onclick='connect()'>Kết nối loa đã "
    "chọn</button>"
    "<button onclick='disconnect()'>Ngắt kết nối</button></div><div class=card><b>2. Phát "
    "thử</b><br>"
    "<button onclick=post('/api/tone')>Phát tiếng thử nhỏ</button><button "
    "onclick=post('/api/stop')>Dừng tiếng thử</button>"
    "</div><div class=card id=status>Đang tải trạng thái…</div><script>"
    "async function post(u,b=''){let r=await "
    "fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b});"
    "let j=await "
    "r.json();document.getElementById('status').innerText=j.message||JSON.stringify(j);refresh()}"
    "async function scan(){await post('/api/scan')}async function connect(){let "
    "x=document.getElementById('devices');"
    "if(x.value)await post('/api/connect',JSON.stringify({address:x.value}));}"
    "async function disconnect(){await post('/api/disconnect')}"
    "async function refresh(){try{let s=await(await "
    "fetch('/api/status',{cache:'no-store'})).json();"
    "document.getElementById('status').innerText='Wi-Fi: '+s.ssid+' (không mật khẩu)  |  IP: "
    "'+s.ip+'\\nBluetooth: "
    "'+(s.switching?'Đang chuyển loa':(s.connected?'Đã kết nối':'Chưa kết nối'))+'  |  Âm thanh: "
    "'+(s.streaming?'Đang phát':'Đang dừng')+'\\nLoa hiện tại: '+(s.current||'—')+'\\nLoa đích: "
    "'+(s.target||'—')+'\\n'+(s.scanning?'Đang quét…\\n':'')+s.message;let "
    "x=document.getElementById('devices'),v=x.value;"
    "x.replaceChildren();for(let p of s.devices){let "
    "o=document.createElement('option');o.value=p.address;"
    "o.textContent=p.name+' — '+p.rssi+' dBm — '+p.address;x.appendChild(o)}if(v)x.value=v;"
    "}catch(e){document.getElementById('status').innerText='Chưa kết nối tới mạch'}}"
    "setInterval(refresh,2000);refresh()</script></html>";

static void post(const event_t* event) {
    if (xQueueSend(events, event, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Event queue full; restart and retry");
    }
}

static void set_tone(bool enabled) {
    portENTER_CRITICAL(&audio_lock);
    tone_cursor = enabled ? 0 : TONE_FRAMES;
    portEXIT_CRITICAL(&audio_lock);
    tone_report_pending = enabled;
    ESP_LOGI(TAG, "%s",
             enabled ? "TEST_TONE: 440 Hz, 6 seconds, peak -26 dBFS" : "TEST_TONE: muted");
}

static void copy_bt_name(char* out, size_t cap, const uint8_t* name, size_t len) {
    size_t used = 0;
    for (size_t i = 0; i < len && used + 1 < cap; ++i) {
        if (name[i] >= 0x20 && name[i] != 0x7f)
            out[used++] = (char)name[i];
    }
    out[used] = 0;
}

static void json_escape(char* out, size_t cap, size_t* used, const char* in) {
    for (const unsigned char* p = (const unsigned char*)in; *p && *used + 7 < cap; ++p) {
        if (*p == '"' || *p == '\\') {
            out[(*used)++] = '\\';
            out[(*used)++] = (char)*p;
        } else if (*p < 0x20) {
            *used += (size_t)snprintf(out + *used, cap - *used, "\\u%04x", *p);
        } else {
            out[(*used)++] = (char)*p;
        }
    }
    out[*used] = 0;
}

static esp_err_t root_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t* req) {
    peer_t snapshot[PEER_COUNT];
    portENTER_CRITICAL(&peers_lock);
    memcpy(snapshot, peers, sizeof(snapshot));
    portEXIT_CRITICAL(&peers_lock);
    char body[4096];
    int n = snprintf(body, sizeof(body),
                     "{\"ssid\":\"%s\",\"ip\":\"192.168.4.1\",\"connected\":%s,"
                     "\"streaming\":%s,\"scanning\":%s,\"switching\":%s,\"current\":\"",
                     AP_SSID, connected ? "true" : "false", streaming ? "true" : "false",
                     (discovering || scan_after_disconnect) ? "true" : "false",
                     switch_pending ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(body))
        return ESP_FAIL;
    size_t used = (size_t)n;
    json_escape(body, sizeof(body), &used, active_target);
    n = snprintf(body + used, sizeof(body) - used, "\",\"target\":\"");
    if (n < 0 || (size_t)n >= sizeof(body) - used)
        return ESP_FAIL;
    used += (size_t)n;
    json_escape(body, sizeof(body), &used, target);
    n = snprintf(body + used, sizeof(body) - used, "\",\"message\":\"");
    if (n < 0 || (size_t)n >= sizeof(body) - used)
        return ESP_FAIL;
    used += (size_t)n;
    json_escape(body, sizeof(body), &used, state_message);
    n = snprintf(body + used, sizeof(body) - used, "\",\"devices\":[");
    if (n < 0 || (size_t)n >= sizeof(body) - used)
        return ESP_FAIL;
    used += (size_t)n;
    bool first = true;
    for (int i = 0; i < PEER_COUNT; ++i) {
        if (!snapshot[i].last_seen)
            continue;
        char address[18];
        snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", snapshot[i].address[0],
                 snapshot[i].address[1], snapshot[i].address[2], snapshot[i].address[3],
                 snapshot[i].address[4], snapshot[i].address[5]);
        n = snprintf(body + used, sizeof(body) - used, "%s{\"address\":\"%s\",\"name\":\"",
                     first ? "" : ",", address);
        if (n < 0 || (size_t)n >= sizeof(body) - used)
            break;
        used += (size_t)n;
        json_escape(body, sizeof(body), &used, snapshot[i].name);
        n = snprintf(body + used, sizeof(body) - used, "\",\"rssi\":%d}", snapshot[i].rssi);
        if (n < 0 || (size_t)n >= sizeof(body) - used)
            break;
        used += (size_t)n;
        first = false;
    }
    if (used + 3 >= sizeof(body))
        return ESP_FAIL;
    memcpy(body + used, "]}", 3);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_handler(httpd_req_t* req) {
    if (req->content_len > 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "request too large");
        return ESP_FAIL;
    }
    char body[65] = {0};
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "request incomplete");
            return ESP_FAIL;
        }
        got += n;
    }
    event_t e = {0};
    if (strcmp(req->uri, "/api/scan") == 0) {
        e.kind = EV_WEB_SCAN;
    } else if (strcmp(req->uri, "/api/tone") == 0) {
        e.kind = EV_COMMAND;
        strcpy(e.text, "tone");
    } else if (strcmp(req->uri, "/api/stop") == 0) {
        e.kind = EV_COMMAND;
        strcpy(e.text, "stop");
    } else if (strcmp(req->uri, "/api/disconnect") == 0) {
        e.kind = EV_WEB_DISCONNECT;
    } else if (strcmp(req->uri, "/api/connect") == 0) {
        e.kind = EV_WEB_CONNECT;
        int parsed = 0;
        unsigned int b[ESP_BD_ADDR_LEN];
        const char* a = strstr(body, "\"address\"");
        if (!a ||
            sscanf(a + 10, "\"%2x:%2x:%2x:%2x:%2x:%2x\"%n", &b[0], &b[1], &b[2], &b[3], &b[4],
                   &b[5], &parsed) != 6 ||
            parsed != 19) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid Bluetooth address");
            return ESP_FAIL;
        }
        for (int i = 0; i < ESP_BD_ADDR_LEN; ++i)
            e.address[i] = (uint8_t)b[i];
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown endpoint");
        return ESP_FAIL;
    }
    if (xQueueSend(events, &e, 0) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "device busy; retry");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(
        req, "{\"ok\":true,\"message\":\"Lệnh đã gửi; trạng thái đang cập nhật.\"}");
}

static void start_wifi_and_web(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* ap = esp_netif_create_default_wifi_ap();
    configASSERT(ap);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    wifi_config_t cfg = {0};
    strcpy((char*)cfg.ap.ssid, AP_SSID);
    cfg.ap.ssid_len = strlen(AP_SSID);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
    server_cfg.max_uri_handlers = 8;
    server_cfg.stack_size = 8192;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &server_cfg));
    httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/scan", .method = HTTP_POST, .handler = post_handler},
        {.uri = "/api/connect", .method = HTTP_POST, .handler = post_handler},
        {.uri = "/api/disconnect", .method = HTTP_POST, .handler = post_handler},
        {.uri = "/api/tone", .method = HTTP_POST, .handler = post_handler},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = post_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    ESP_LOGI(TAG, "WIFI_AP: ssid=%s security=open ip=192.168.4.1", AP_SSID);
    ESP_LOGI(TAG, "WEB: open http://192.168.4.1/ after joining the Wi-Fi above");
}

// Negotiate 44.1 kHz / two channels before feeding the IDF internal SBC encoder.
// Reserve a bounded sample range under the lock; render without allocation or logging.
static int32_t audio_data(uint8_t* data, int32_t length) {
    if (!data || length <= 0)
        return 0;
    memset(data, 0, length);
    uint32_t frames = (uint32_t)length / 4;
    portENTER_CRITICAL(&audio_lock);
    uint32_t start = tone_cursor;
    uint32_t active = TONE_FRAMES - start;
    if (active > frames)
        active = frames;
    tone_cursor += active;
    pcm_frames += frames;
    portEXIT_CRITICAL(&audio_lock);
    for (uint32_t i = 0; i < active; ++i) {
        uint32_t pos = start + i;
        uint32_t beat = pos % SAMPLE_RATE;
        if (beat >= SAMPLE_RATE / 2)
            continue;
        int32_t envelope = 220;
        if (beat < 220)
            envelope = beat;
        if (SAMPLE_RATE / 2 - beat < 220)
            envelope = SAMPLE_RATE / 2 - beat;
        uint32_t phase = (uint32_t)(((uint64_t)pos * 440U * 256U / SAMPLE_RATE) % 256);
        int16_t sample = sine_table[phase] * envelope / 220;
        memcpy(data + i * 4, &sample, 2);
        memcpy(data + i * 4 + 2, &sample, 2);
    }
    return length;
}

static void gap_callback(esp_bt_gap_cb_event_t type, esp_bt_gap_cb_param_t* p) {
    event_t e = {0};
    switch (type) {
        case ESP_BT_GAP_DISC_RES_EVT:
            e.kind = EV_FOUND;
            memcpy(e.address, p->disc_res.bda, ESP_BD_ADDR_LEN);
            e.value = -129;
            for (int i = 0; i < p->disc_res.num_prop; ++i) {
                esp_bt_gap_dev_prop_t* prop = &p->disc_res.prop[i];
                if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
                    e.value = *(int8_t*)prop->val;
                } else if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    copy_bt_name(e.text, sizeof(e.text), prop->val, prop->len);
                } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    uint8_t n = 0;
                    uint8_t* name =
                        esp_bt_gap_resolve_eir_data(prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &n);
                    if (!name)
                        name = esp_bt_gap_resolve_eir_data(prop->val,
                                                           ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &n);
                    if (name) {
                        copy_bt_name(e.text, sizeof(e.text), name, n);
                    }
                }
            }
            post(&e);
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            e.kind = EV_DISCOVERY;
            e.value = p->disc_st_chg.state;
            post(&e);
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            ESP_LOGI(TAG, "PAIRING: %s, status=%d",
                     p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "SUCCESS" : "FAILED",
                     p->auth_cmpl.stat);
            break;
        case ESP_BT_GAP_PIN_REQ_EVT: {
            esp_bt_pin_code_t pin = {0};
            memcpy(pin, legacy_pin, 4);
            esp_bt_gap_pin_reply(p->pin_req.bda, !p->pin_req.min_16_digit, 4, pin);
            break;
        }
        case ESP_BT_GAP_CFM_REQ_EVT:
            // No-input/no-output test device: Just Works pairing with selected speaker.
            esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
            break;
        default:
            break;
    }
}

static void a2dp_callback(esp_a2d_cb_event_t type, esp_a2d_cb_param_t* p) {
    event_t e = {0};
    switch (type) {
        case ESP_A2D_PROF_STATE_EVT:
            e.kind = EV_PROFILE;
            e.value = p->a2d_prof_stat.init_state;
            break;
        case ESP_A2D_CONNECTION_STATE_EVT:
            e.kind = EV_CONNECTION;
            e.value = p->conn_stat.state;
            e.detail = p->conn_stat.disc_rsn;
            memcpy(e.address, p->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            e.kind = EV_AUDIO;
            e.value = p->audio_stat.state;
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            e.kind = EV_ACK;
            e.value = p->media_ctrl_stat.cmd;
            e.detail = p->media_ctrl_stat.status;
            break;
        case ESP_A2D_REPORT_SNK_CODEC_CAPS_EVT:
            e.kind = EV_CODEC_CAPS;
            e.value = p->a2d_report_snk_codec_caps_stat.conn_hdl;
            e.codec = p->a2d_report_snk_codec_caps_stat.mcc;
            break;
        case ESP_A2D_SRC_SET_PREF_MCC_EVT:
            e.kind = EV_CODEC_SET;
            e.value = p->a2d_set_pref_mcc_stat.set_status;
            break;
        default:
            return;
    }
    post(&e);
}

static void scan(void) {
    if (!ready || discovering || connecting || connected) {
        ESP_LOGI(TAG, "Scan skipped: profile busy or not ready");
        return;
    }
    found = false;
    portENTER_CRITICAL(&peers_lock);
    memset(peers, 0, sizeof(peers));
    portEXIT_CRITICAL(&peers_lock);
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0);
    discovering = err == ESP_OK;
    next_action = esp_timer_get_time() + 3000000;
    ESP_LOGI(TAG, "SCAN: target=\"%s\", result=%s", target, esp_err_to_name(err));
}

static void disconnect_for_scan(void) {
    if (!scan_after_disconnect)
        return;
    if (!connected && !connecting) {
        scan_after_disconnect = false;
        scan_disconnect_attempts = 0;
        scan();
        return;
    }

    esp_err_t err = esp_a2d_source_disconnect(peer);
    ++scan_disconnect_attempts;
    scan_disconnect_deadline = esp_timer_get_time() + (err == ESP_OK ? 5000000 : 2000000);
    ESP_LOGI(TAG, "SCAN_AFTER_DISCONNECT: attempt=%u, %s", scan_disconnect_attempts,
             esp_err_to_name(err));
}

static void request_scan(void) {
    if (discovering) {
        strlcpy(state_message, "Đang quét thiết bị Bluetooth.", sizeof(state_message));
        return;
    }

    if (connected || connecting || switch_pending) {
        scan_after_disconnect = true;
        scan_disconnect_attempts = 0;
        switch_pending = false;
        switch_connecting = false;
        switch_waiting_suspend = false;
        switch_disconnect_requested = false;
        set_tone(false);
        auto_tone_played = true;
        if (connected && streaming)
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
        strlcpy(state_message, "Đang ngắt loa hiện tại để quét lại thiết bị.",
                sizeof(state_message));
        disconnect_for_scan();
        return;
    }

    scan_after_disconnect = false;
    scan_disconnect_attempts = 0;
    scan();
}

static void status(void) {
    portENTER_CRITICAL(&audio_lock);
    uint32_t frames = pcm_frames, cursor = tone_cursor;
    portEXIT_CRITICAL(&audio_lock);
    ESP_LOGI(TAG,
             "STATUS: selected=\"%s\" connected=%d streaming=%d scanning=%d "
             "tone_remaining_ms=%" PRIu32 " pcm_frames=%" PRIu32 " heap=%" PRIu32,
             target, connected, streaming, discovering, (TONE_FRAMES - cursor) * 1000 / SAMPLE_RATE,
             frames, esp_get_free_heap_size());
}

static bool same_peer(const esp_bd_addr_t a, const esp_bd_addr_t b) {
    return memcmp(a, b, ESP_BD_ADDR_LEN) == 0;
}

static void start_pending_connection(void) {
    if (!switch_pending || connected || connecting || discovering)
        return;

    memcpy(peer, pending_peer, sizeof(peer));
    strlcpy(target, pending_name, sizeof(target));
    switch_waiting_suspend = false;
    switch_disconnect_requested = false;
    switch_connecting = true;
    esp_err_t err = esp_a2d_source_connect(peer);
    connecting = err == ESP_OK;
    if (err != ESP_OK) {
        switch_connecting = false;
        switch_pending = false;
        snprintf(state_message, sizeof(state_message), "Không bắt đầu được kết nối tới %.48s (%s).",
                 target, esp_err_to_name(err));
        ESP_LOGE(TAG, "SWITCH_CONNECT: %s, %s", target, esp_err_to_name(err));
        return;
    }

    next_action = esp_timer_get_time() + 20000000;
    snprintf(state_message, sizeof(state_message), "Đang kết nối tới loa %.64s.", target);
    ESP_LOGI(TAG, "SWITCH_CONNECT: %s", target);
}

static void disconnect_current_for_switch(void) {
    if (!switch_pending || switch_connecting || switch_disconnect_requested)
        return;
    if (!connected && !connecting) {
        start_pending_connection();
        return;
    }

    switch_waiting_suspend = false;
    esp_err_t err = esp_a2d_source_disconnect(peer);
    ++switch_disconnect_attempts;
    switch_deadline = esp_timer_get_time() + (err == ESP_OK ? 8000000 : 2000000);
    if (err == ESP_OK) {
        switch_disconnect_requested = true;
        snprintf(state_message, sizeof(state_message),
                 "Đã yêu cầu ngắt loa cũ trước khi chuyển sang %.48s.", pending_name);
        ESP_LOGI(TAG, "SWITCH_DISCONNECT: old=%s new=%s", active_target, pending_name);
    } else {
        snprintf(state_message, sizeof(state_message), "Đang thử ngắt loa cũ (%s).",
                 esp_err_to_name(err));
        ESP_LOGW(TAG, "SWITCH_DISCONNECT: %s", esp_err_to_name(err));
    }
}

static void begin_switch(const esp_bd_addr_t address, const char* name) {
    if (switch_pending && switch_connecting) {
        if (same_peer(address, pending_peer)) {
            snprintf(state_message, sizeof(state_message), "Đang kết nối tới loa %.64s.",
                     pending_name);
            return;
        }
        // If the user picks another speaker during connection setup, cancel that
        // in-flight connection first. The latest selection remains queued.
        esp_err_t err = esp_a2d_source_disconnect(peer);
        switch_connecting = false;
        switch_disconnect_requested = err == ESP_OK;
        switch_disconnect_attempts = 1;
        switch_deadline = esp_timer_get_time() + (err == ESP_OK ? 8000000 : 2000000);
        memcpy(pending_peer, address, sizeof(pending_peer));
        strlcpy(pending_name, name, sizeof(pending_name));
        strlcpy(target, name, sizeof(target));
        snprintf(state_message, sizeof(state_message),
                 "Đang hủy kết nối trước khi chuyển sang %.48s.", pending_name);
        ESP_LOGI(TAG, "SWITCH_CANCEL_CONNECT: next=%s, %s", pending_name, esp_err_to_name(err));
        return;
    }

    memcpy(pending_peer, address, sizeof(pending_peer));
    strlcpy(pending_name, name, sizeof(pending_name));
    strlcpy(target, name, sizeof(target));
    switch_pending = true;
    switch_disconnect_attempts = 0;
    switch_disconnect_requested = false;
    switch_connecting = false;
    set_tone(false);

    if (discovering)
        esp_bt_gap_cancel_discovery();

    if (connected && streaming) {
        switch_waiting_suspend = true;
        switch_deadline = esp_timer_get_time() + 2000000;
        esp_err_t err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
        snprintf(state_message, sizeof(state_message),
                 "Đang dừng âm thanh trên loa cũ trước khi chuyển sang %.48s.", pending_name);
        ESP_LOGI(TAG, "SWITCH_SUSPEND: old=%s new=%s, %s", active_target, pending_name,
                 esp_err_to_name(err));
        if (err != ESP_OK)
            disconnect_current_for_switch();
    } else if (connected || connecting) {
        disconnect_current_for_switch();
    } else if (discovering) {
        // The discovery callback will launch the selected connection once the
        // scan has stopped; keep the normal scan completion path in charge.
        strlcpy(state_message, "Đang dừng quét để kết nối loa đã chọn.", sizeof(state_message));
    } else {
        start_pending_connection();
    }
}

static void save_and_restart(const char* key, const char* value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bt_test", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, key, value);
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
        return;
    }
    set_tone(false);
    ESP_LOGI(TAG, "Saved %s; restarting", key);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void command(const char* s) {
    if (strcmp(s, "tone") == 0) {
        if (connected && streaming)
            set_tone(true);
        else
            ESP_LOGW(TAG, "Wait for AUDIO: STARTED before tone");
    } else if (strcmp(s, "stop") == 0) {
        auto_tone_played = true;
        set_tone(false);
    } else if (strcmp(s, "status") == 0) {
        status();
    } else if (strcmp(s, "scan") == 0) {
        request_scan();
    } else if (strncmp(s, "pin ", 4) == 0 && strlen(s + 4) == 4 &&
               strspn(s + 4, "0123456789") == 4) {
        save_and_restart("pin", s + 4);
    } else if (strcmp(s, "restart") == 0) {
        esp_restart();
    } else {
        ESP_LOGI(TAG, "Commands: status | tone | stop | scan | pin <4 digits> | restart");
    }
}

static void console_task(void* arg) {
    event_t e = {.kind = EV_COMMAND};
    size_t used = 0;
    bool overflow = false;
    while (true) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (c == '\r' || c == '\n') {
            if (used && !overflow) {
                e.text[used] = 0;
                post(&e);
            }
            used = 0;
            overflow = false;
        } else if (c == 8 || c == 127) {
            if (used)
                --used;
        } else if (c >= 32 && c <= 126) {
            if (used < sizeof(e.text) - 1)
                e.text[used++] = c;
            else
                overflow = true;
        }
    }
}

static void process(const event_t* e) {
    switch (e->kind) {
        case EV_CODEC_CAPS: {
            if (e->codec.type != ESP_A2D_MCT_SBC)
                break;
            esp_a2d_mcc_t cfg = e->codec;
            esp_a2d_cie_sbc_t* sbc = &cfg.cie.sbc_info;
            if (!(sbc->samp_freq & ESP_A2D_SBC_CIE_SF_44K) ||
                !(sbc->block_len & ESP_A2D_SBC_CIE_BLOCK_LEN_16) ||
                !(sbc->num_subbands & ESP_A2D_SBC_CIE_NUM_SUBBANDS_8) ||
                !(sbc->alloc_mthd & ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS)) {
                ESP_LOGE(TAG, "Speaker lacks test codec configuration; no tone started");
                break;
            }
            if (sbc->ch_mode & ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO)
                sbc->ch_mode = ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
            else if (sbc->ch_mode & ESP_A2D_SBC_CIE_CH_MODE_STEREO)
                sbc->ch_mode = ESP_A2D_SBC_CIE_CH_MODE_STEREO;
            else {
                ESP_LOGE(TAG, "Speaker lacks stereo mode; no tone started");
                break;
            }
            sbc->samp_freq = ESP_A2D_SBC_CIE_SF_44K;
            sbc->block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
            sbc->num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
            sbc->alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
            if (sbc->min_bitpool < 2)
                sbc->min_bitpool = 2;
            if (sbc->max_bitpool > 53)
                sbc->max_bitpool = 53;
            if (sbc->min_bitpool > sbc->max_bitpool)
                break;
            esp_err_t err = esp_a2d_source_set_pref_mcc(e->value, &cfg);
            ESP_LOGI(TAG, "CODEC_REQUEST: 44100 Hz stereo, %s", esp_err_to_name(err));
            break;
        }
        case EV_CODEC_SET:
            codec_ready = e->value == ESP_BT_STATUS_SUCCESS;
            ESP_LOGI(TAG, "CODEC: ready=%d status=%d", codec_ready, e->value);
            break;
        case EV_PROFILE:
            ready = e->value == ESP_A2D_INIT_SUCCESS;
            ESP_LOGI(TAG, "PROFILE: ready=%d", ready);
            if (ready)
                scan();
            break;
        case EV_FOUND:
            ESP_LOGI(TAG, "FOUND: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d name=\"%s\"", e->address[0],
                     e->address[1], e->address[2], e->address[3], e->address[4], e->address[5],
                     e->value, e->text);
            portENTER_CRITICAL(&peers_lock);
            int slot = -1;
            int oldest = 0;
            for (int i = 0; i < PEER_COUNT; ++i) {
                if (peers[i].last_seen &&
                    memcmp(peers[i].address, e->address, ESP_BD_ADDR_LEN) == 0) {
                    slot = i;
                    break;
                }
                if (!peers[i].last_seen)
                    oldest = i;
                else if (peers[oldest].last_seen && peers[i].last_seen < peers[oldest].last_seen)
                    oldest = i;
            }
            if (slot < 0)
                slot = oldest;
            memcpy(peers[slot].address, e->address, ESP_BD_ADDR_LEN);
            if (e->text[0])
                strlcpy(peers[slot].name, e->text, sizeof(peers[slot].name));
            peers[slot].rssi = e->value;
            peers[slot].last_seen = esp_timer_get_time();
            portEXIT_CRITICAL(&peers_lock);
            break;
        case EV_DISCOVERY:
            discovering = e->value == ESP_BT_GAP_DISCOVERY_STARTED;
            if (!discovering && switch_pending) {
                start_pending_connection();
            } else if (!discovering && found && !connected && !connecting) {
                esp_err_t err = esp_a2d_source_connect(peer);
                connecting = err == ESP_OK;
                next_action = esp_timer_get_time() + 20000000;
                ESP_LOGI(TAG, "CONNECT: %s, %s", target, esp_err_to_name(err));
                snprintf(state_message, sizeof(state_message), "Đang kết nối tới loa %.64s.",
                         target);
            } else if (!discovering) {
                next_action = esp_timer_get_time() + 3000000;
            }
            break;
        case EV_CONNECTION:
            ESP_LOGI(TAG, "A2DP: state=%d reason=%d", e->value, e->detail);
            if (e->value == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                if (scan_after_disconnect) {
                    connecting = false;
                    connected = true;
                    memcpy(peer, e->address, sizeof(peer));
                    strlcpy(active_target, target, sizeof(active_target));
                    set_tone(false);
                    disconnect_for_scan();
                    break;
                }
                if (switch_pending && !same_peer(e->address, pending_peer)) {
                    // A late CONNECTED for the old destination arrived while its
                    // cancellation was in flight. Keep the requested destination
                    // queued and disconnect this link before connecting it.
                    connecting = false;
                    connected = true;
                    switch_connecting = false;
                    memcpy(peer, e->address, sizeof(peer));
                    disconnect_current_for_switch();
                    break;
                }
                connecting = false;
                connected = true;
                memcpy(peer, e->address, sizeof(peer));
                if (switch_pending && same_peer(e->address, pending_peer)) {
                    strlcpy(target, pending_name, sizeof(target));
                    switch_pending = false;
                    switch_connecting = false;
                    switch_waiting_suspend = false;
                    switch_disconnect_requested = false;
                    switch_disconnect_attempts = 0;
                    strlcpy(state_message, "Đã kết nối đúng loa đã chọn.", sizeof(state_message));
                } else {
                    strlcpy(active_target, target, sizeof(active_target));
                    strlcpy(state_message, "A2DP đã kết nối; đang thương lượng codec.",
                            sizeof(state_message));
                }
                strlcpy(active_target, target, sizeof(active_target));
                auto_tone_played = false;
                next_action = esp_timer_get_time() + 1000000;
                ESP_LOGI(TAG, "CONNECTED: %s", target);
            } else if (e->value == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                if (!same_peer(e->address, peer)) {
                    ESP_LOGW(TAG, "Ignoring stale DISCONNECTED event for another peer");
                    break;
                }
                bool pending_connect_failed = switch_connecting;
                connected = connecting = streaming = false;
                codec_ready = false;
                set_tone(false);
                active_target[0] = 0;
                switch_disconnect_requested = false;
                switch_waiting_suspend = false;
                switch_connecting = false;
                if (scan_after_disconnect) {
                    strlcpy(state_message, "Loa đã ngắt; bắt đầu quét thiết bị Bluetooth.",
                            sizeof(state_message));
                    scan_after_disconnect = false;
                    scan_disconnect_attempts = 0;
                    scan();
                    break;
                }
                if (switch_pending && !pending_connect_failed) {
                    snprintf(state_message, sizeof(state_message),
                             "Loa cũ đã ngắt; đang chuyển sang %.64s.", pending_name);
                    start_pending_connection();
                } else if (pending_connect_failed) {
                    switch_pending = false;
                    snprintf(state_message, sizeof(state_message), "Kết nối tới %.48s bị ngắt.",
                             target);
                    next_action = esp_timer_get_time() + 5000000;
                } else {
                    strlcpy(state_message, "Loa Bluetooth đã ngắt kết nối.", sizeof(state_message));
                    next_action = esp_timer_get_time() + 5000000;
                }
            }
            break;
        case EV_AUDIO:
            streaming = e->value == ESP_A2D_AUDIO_STATE_STARTED;
            ESP_LOGI(TAG, "AUDIO: %s (state=%d)", streaming ? "STARTED" : "STOPPED", e->value);
            if (streaming && !auto_tone_played && !scan_after_disconnect) {
                auto_tone_played = true;
                set_tone(true);
            }
            break;
        case EV_ACK:
            ESP_LOGI(TAG, "MEDIA_ACK: command=%d status=%d", e->value, e->detail);
            if (switch_pending && switch_waiting_suspend &&
                e->value == ESP_A2D_MEDIA_CTRL_SUSPEND) {
                ESP_LOGI(TAG, "SWITCH_SUSPEND_ACK: status=%d", e->detail);
                disconnect_current_for_switch();
                break;
            }
            if (connected && !scan_after_disconnect &&
                e->value == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                e->detail == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                next_action = esp_timer_get_time() + 5000000;
            }
            break;
        case EV_COMMAND:
            command(e->text);
            break;
        case EV_WEB_SCAN:
            request_scan();
            break;
        case EV_WEB_CONNECT: {
            char selected[sizeof(target)] = "Bluetooth device";
            bool known = false;
            portENTER_CRITICAL(&peers_lock);
            for (int i = 0; i < PEER_COUNT; ++i) {
                if (peers[i].last_seen &&
                    memcmp(peers[i].address, e->address, ESP_BD_ADDR_LEN) == 0) {
                    strlcpy(selected, peers[i].name, sizeof(selected));
                    known = true;
                    break;
                }
            }
            portEXIT_CRITICAL(&peers_lock);
            if (!ready || !known) {
                strlcpy(state_message,
                        !ready ? "Bluetooth chưa sẵn sàng."
                               : "Hãy quét lại rồi chọn loa trong danh sách.",
                        sizeof(state_message));
                ESP_LOGW(TAG, "CONNECT rejected: profile not ready or peer not in scan list");
                break;
            }
            if (scan_after_disconnect) {
                strlcpy(state_message, "Đang quét lại; hãy chờ danh sách thiết bị mới.",
                        sizeof(state_message));
                break;
            }
            if (!switch_pending && (connected || connecting) && same_peer(peer, e->address)) {
                strlcpy(target, selected, sizeof(target));
                strlcpy(state_message,
                        connected ? "Đang kết nối đúng loa này rồi." : "Đang kết nối tới loa này.",
                        sizeof(state_message));
                break;
            }
            begin_switch(e->address, selected);
            break;
        }
        case EV_WEB_DISCONNECT:
            if (discovering)
                esp_bt_gap_cancel_discovery();
            scan_after_disconnect = false;
            scan_disconnect_attempts = 0;
            switch_pending = false;
            switch_connecting = false;
            switch_waiting_suspend = false;
            switch_disconnect_requested = false;
            switch_disconnect_attempts = 0;
            if (connected || connecting) {
                auto_tone_played = true;
                set_tone(false);
                esp_a2d_source_disconnect(peer);
                strlcpy(state_message, "Đang ngắt kết nối loa Bluetooth.", sizeof(state_message));
            } else {
                strlcpy(state_message, "Không có loa đang kết nối.", sizeof(state_message));
            }
            break;
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A full flash backup is taken before initial installation.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_handle_t handle;
    if (nvs_open("bt_test", NVS_READONLY, &handle) == ESP_OK) {
        size_t n = sizeof(legacy_pin);
        nvs_get_str(handle, "pin", legacy_pin, &n);
        nvs_close(handle);
    }
    for (int i = 0; i < 256; ++i)
        sine_table[i] = (int16_t)(1600 * sinf(i * 6.2831853f / 256));
    events = xQueueCreate(24, sizeof(event_t));
    configASSERT(events);
    start_wifi_and_web();
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("ESP32-D1-Bluetooth"));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));
    esp_bt_io_cap_t cap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &cap, sizeof(cap)));
    esp_bt_pin_code_t pin = {0};
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2dp_callback));
    ESP_ERROR_CHECK(esp_a2d_source_register_data_callback(audio_data));
    ESP_ERROR_CHECK(esp_a2d_source_init());
    configASSERT(xTaskCreate(console_task, "console", 3072, NULL, 3, NULL) == pdPASS);
    ESP_LOGI(TAG, "BOOT: ESP32-D1-Bluetooth A2DP Source; choose a speaker at http://192.168.4.1/");
    command("help");
    while (true) {
        event_t e;
        if (xQueueReceive(events, &e, pdMS_TO_TICKS(500)) == pdTRUE)
            process(&e);
        int64_t now = esp_timer_get_time();
        if (ready && scan_after_disconnect && now >= scan_disconnect_deadline) {
            if (scan_disconnect_attempts >= 3) {
                scan_after_disconnect = false;
                strlcpy(state_message, "Không ngắt được loa để quét; hãy thử Ngắt kết nối.",
                        sizeof(state_message));
                ESP_LOGE(TAG, "Scan aborted: could not disconnect active speaker");
            } else {
                disconnect_for_scan();
            }
        }
        if (ready && switch_pending) {
            if (switch_waiting_suspend && now >= switch_deadline) {
                ESP_LOGW(TAG, "SWITCH_SUSPEND timeout; disconnecting old speaker directly");
                disconnect_current_for_switch();
            } else if (switch_disconnect_requested && now >= switch_deadline) {
                if (switch_disconnect_attempts >= 3) {
                    switch_pending = false;
                    switch_disconnect_requested = false;
                    strlcpy(state_message, "Không ngắt được loa cũ; vẫn giữ kết nối hiện tại.",
                            sizeof(state_message));
                    ESP_LOGE(TAG, "Speaker switch aborted after disconnect retries");
                } else {
                    switch_disconnect_requested = false;
                    disconnect_current_for_switch();
                }
            } else if (!switch_connecting && !switch_disconnect_requested &&
                       (connected || connecting)) {
                disconnect_current_for_switch();
            } else if (!switch_connecting && !connected && !connecting && !discovering) {
                start_pending_connection();
            } else if (switch_connecting && now >= next_action) {
                esp_a2d_source_disconnect(peer);
                connecting = false;
                switch_connecting = false;
                switch_pending = false;
                strlcpy(state_message, "Kết nối tới loa đã chọn quá thời gian chờ.",
                        sizeof(state_message));
                next_action = now + 5000000;
            }
        }

        if (ready && !switch_pending && now >= next_action) {
            if (connecting) {
                ESP_LOGW(TAG, "Connection timeout; cancel and rescan");
                esp_a2d_source_disconnect(peer);
                connecting = false;
                next_action = now + 5000000;
            } else if (connected && codec_ready && !streaming && !scan_after_disconnect) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
                next_action = now + 5000000;
            } else if (!connected && !discovering) {
                scan();
            }
        }
        portENTER_CRITICAL(&audio_lock);
        bool done = tone_cursor == TONE_FRAMES;
        portEXIT_CRITICAL(&audio_lock);
        if (tone_report_pending && done) {
            tone_report_pending = false;
            ESP_LOGI(TAG, "TEST_TONE: complete; silent stream retained. Use tone to replay.");
            status();
        }
    }
}
