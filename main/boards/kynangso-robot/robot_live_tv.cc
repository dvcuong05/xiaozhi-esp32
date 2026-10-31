#include "robot_live_tv.h"

#include "application.h"
#include "display/lvgl_display/jpg/jpeg_to_image.h"
#include "display/lvgl_display/lvgl_image.h"
#include "kynangso_robot_display.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <freertos/idf_additions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <utility>

namespace kynangso {
namespace {

constexpr char TAG[] = "RobotLiveTv";
constexpr size_t kMaxJpegBytes = 128 * 1024;
constexpr size_t kReadBufferBytes = 2048;
constexpr size_t kMaxHeaderLine = 256;
constexpr TickType_t kRadioStartTimeout = pdMS_TO_TICKS(60000);
constexpr uint32_t kStreamTimeoutMs = 15000;
constexpr TickType_t kVideoReadGrace = pdMS_TO_TICKS(kStreamTimeoutMs);
constexpr uint32_t kVideoReconnectInitialDelayMs = 1000;
constexpr uint32_t kVideoReconnectMaxDelayMs = 8000;
constexpr int64_t kStatsWindowUs = 5000000;

bool IsHttpUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

bool IsBoundedJpeg(const uint8_t* jpeg, size_t size) {
    if (size < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8)
        return false;
    size_t cursor = 2;
    while (cursor + 4 < size) {
        if (jpeg[cursor++] != 0xff)
            return false;
        while (cursor < size && jpeg[cursor] == 0xff)
            ++cursor;
        if (cursor >= size)
            return false;
        uint8_t marker = jpeg[cursor++];
        if (marker == 0xda || marker == 0xd9 || cursor + 2 > size)
            return false;
        size_t length = (jpeg[cursor] << 8) | jpeg[cursor + 1];
        if (length < 2 || length > size - cursor)
            return false;
        if (marker == 0xc0) {
            if (length < 8 || jpeg[cursor + 2] != 8)
                return false;
            size_t height = (jpeg[cursor + 3] << 8) | jpeg[cursor + 4];
            size_t width = (jpeg[cursor + 5] << 8) | jpeg[cursor + 6];
            return width > 0 && width <= 240 && height > 0 && height <= 240;
        }
        cursor += length;
    }
    return false;
}

// One small window, no per-frame logs or history buffers. "refreshed" counts
// LVGL refresh completions, not physical pixels observed on the panel.
class FrameStats {
public:
    uint32_t FpsX100() const { return fps_x100_; }
    void Add(uint32_t bytes, int64_t read_us, int64_t decode_us, int64_t present_us, bool submitted,
             KyNangSoRobotDisplay& display) {
        const int64_t now = esp_timer_get_time();
        ++received_;
        submitted_ += submitted;
        bytes_ += bytes;
        read_us_ += read_us;
        decode_us_ += decode_us;
        present_us_ += present_us;
        if (last_frame_us_ != 0)
            max_gap_us_ = std::max(max_gap_us_, now - last_frame_us_);
        last_frame_us_ = now;
        if (now - started_us_ < kStatsWindowUs)
            return;
        const uint32_t refreshed = display.LiveTvRefreshCount();
        fps_x100_ = static_cast<uint32_t>((refreshed - previous_refreshed_) * 100000000.0 /
                                          (now - started_us_));
        wifi_ap_record_t access_point = {};
        const bool wifi_ready = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
        ESP_LOGI(TAG,
                 "Video stats: received=%lu submitted=%lu refreshed=%lu fps=%.2f "
                 "read=%.1f decode=%.1f present=%.1f gap_max=%lld ms jpeg_avg=%lu B "
                 "internal_free=%u largest=%u rssi=%d stack_free=%u",
                 static_cast<unsigned long>(received_), static_cast<unsigned long>(submitted_),
                 static_cast<unsigned long>(refreshed - previous_refreshed_),
                 (refreshed - previous_refreshed_) * 1000000.0 / (now - started_us_),
                 read_us_ / (received_ * 1000.0), decode_us_ / (received_ * 1000.0),
                 present_us_ / (received_ * 1000.0), static_cast<long long>(max_gap_us_ / 1000),
                 static_cast<unsigned long>(bytes_ / received_),
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 wifi_ready ? access_point.rssi : 0,
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        started_us_ = now;
        previous_refreshed_ = refreshed;
        received_ = submitted_ = bytes_ = 0;
        read_us_ = decode_us_ = present_us_ = max_gap_us_ = 0;
    }

private:
    int64_t started_us_ = esp_timer_get_time();
    int64_t last_frame_us_ = 0;
    int64_t read_us_ = 0, decode_us_ = 0, present_us_ = 0, max_gap_us_ = 0;
    uint32_t received_ = 0, submitted_ = 0, bytes_ = 0, previous_refreshed_ = 0;
    uint32_t fps_x100_ = 0;
};

class HttpStreamReader {
public:
    HttpStreamReader(esp_http_client_handle_t client, const std::atomic<bool>& active)
        : client_(client), active_(active) {}

    bool ReadLine(std::string& line) {
        line.clear();
        while (active_.load()) {
            uint8_t byte = 0;
            if (!ReadByte(byte))
                return false;
            if (byte == '\n') {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return true;
            }
            if (line.size() >= kMaxHeaderLine)
                return false;
            line.push_back(static_cast<char>(byte));
        }
        return false;
    }

    bool ReadExact(uint8_t* output, size_t length) {
        size_t copied = 0;
        while (copied < length && active_.load()) {
            if (offset_ == available_ && !Fill())
                return false;
            const size_t count = std::min(length - copied, available_ - offset_);
            std::copy_n(buffer_.data() + offset_, count, output + copied);
            copied += count;
            offset_ += count;
        }
        return copied == length;
    }

private:
    bool ReadByte(uint8_t& byte) {
        if (offset_ == available_ && !Fill())
            return false;
        byte = buffer_[offset_++];
        return true;
    }

    bool Fill() {
        const TickType_t waiting_since = xTaskGetTickCount();
        while (active_.load()) {
            const int received = esp_http_client_read(
                client_, reinterpret_cast<char*>(buffer_.data()), buffer_.size());
            if (received > 0) {
                offset_ = 0;
                available_ = static_cast<size_t>(received);
                return true;
            }
            if (received == -ESP_ERR_HTTP_EAGAIN &&
                xTaskGetTickCount() - waiting_since < kVideoReadGrace) {
                continue;
            }
            if (active_.load())
                ESP_LOGW(TAG, "Video HTTP read stalled/failed: result=%d, complete=%d", received,
                         esp_http_client_is_complete_data_received(client_));
            break;
        }
        return false;
    }

    esp_http_client_handle_t client_;
    const std::atomic<bool>& active_;
    std::array<uint8_t, kReadBufferBytes> buffer_{};
    size_t offset_ = 0;
    size_t available_ = 0;
};

bool ReadMjpegFrame(HttpStreamReader& reader, uint8_t* jpeg, size_t& jpeg_size,
                    const std::atomic<bool>& active) {
    std::string line;
    size_t content_length = 0;
    bool found_boundary = false;
    while (active.load() && reader.ReadLine(line)) {
        if (line.rfind("--", 0) == 0) {
            found_boundary = true;
            break;
        }
    }
    if (!found_boundary)
        return false;

    bool found_length = false;
    while (active.load() && reader.ReadLine(line)) {
        if (line.empty())
            break;
        constexpr char kLengthHeader[] = "content-length:";
        if (line.size() >= sizeof(kLengthHeader) - 1) {
            std::string key = line.substr(0, sizeof(kLengthHeader) - 1);
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (key == kLengthHeader) {
                const char* value = line.c_str() + sizeof(kLengthHeader) - 1;
                while (*value == ' ' || *value == '\t')
                    ++value;
                char* end = nullptr;
                const unsigned long parsed = std::strtoul(value, &end, 10);
                if (end == value || parsed == 0 || parsed > kMaxJpegBytes)
                    return false;
                content_length = static_cast<size_t>(parsed);
                found_length = true;
            }
        }
    }
    if (!found_length || !reader.ReadExact(jpeg, content_length))
        return false;
    jpeg_size = content_length;
    return true;
}

}  // namespace

RobotLiveTv::RobotLiveTv(Application& application, KyNangSoRobotDisplay& display)
    : application_(application), display_(display) {}

RobotLiveTv::~RobotLiveTv() {
    if (active_.exchange(false) && !display_benchmark_)
        application_.RequestStopRadio();
    while (task_.load() != nullptr)
        vTaskDelay(pdMS_TO_TICKS(20));
}

bool RobotLiveTv::Start(std::string title, std::string video_url, std::string audio_url,
                        std::string* error, std::string session_id, std::string channel_id) {
    auto reject = [error](const char* message) {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (title.empty() || title.size() > 128 || !IsHttpUrl(video_url) || video_url.size() > 767 ||
        !IsHttpUrl(audio_url) || audio_url.size() > 767) {
        return reject("tv.start requires title and valid HTTP(S) stream URLs");
    }
    if (!display_.CanStartLiveTv())
        return reject(
            "Robot đang khởi động hoặc có cảnh báo đồng hồ. Hãy chờ hoặc tắt cảnh báo rồi thử "
            "lại.");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_.load() || task_.load() != nullptr)
            return reject("a live TV session is already active");
        title_ = std::move(title);
        video_url_ = std::move(video_url);
        display_benchmark_ = false;
        session_id_ = std::move(session_id);
        channel_id_ = std::move(channel_id);
        last_error_.clear();
        reconnecting_.store(false);
        frames_.store(0);
        fps_x100_.store(0);
        active_.store(true);
    }

    const auto display_title = title_;
    application_.Schedule(
        [this, display_title]() { display_.SetLiveTvActive(true, display_title); });
    MediaPlaybackMetadata metadata;
    metadata.title = title_;
    metadata.station = title_;
    metadata.codec = "HLS";
    StreamPlaybackOptions options;
    options.external_task_stack = true;
    options.http_timeout_ms = kStreamTimeoutMs;
    application_.RequestRadio(std::move(audio_url), std::move(metadata), options);

    TaskHandle_t task = nullptr;
    // This board has PSRAM and permits external task stacks. The removable TV
    // worker performs no flash/NVS writes; keep its 8 KiB stack out of DMA RAM.
    const BaseType_t created = xTaskCreateWithCaps(&TaskEntry, "live_tv", 8192, this, 2, &task,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        active_.store(false);
        application_.RequestStopRadio();
        application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
        return reject("could not start the live TV video worker");
    }
    task_.store(task);
    xTaskNotifyGive(task);
    ESP_LOGI(TAG, "Live TV accepted: title=%s, network timeout=%lu ms", title_.c_str(),
             static_cast<unsigned long>(kStreamTimeoutMs));
    return true;
}

bool RobotLiveTv::Stop() {
    if (!active_.exchange(false))
        return false;
    if (!display_benchmark_)
        application_.RequestStopRadio();
    application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
    ESP_LOGI(TAG, "Live TV stop queued");
    return true;
}

std::string RobotLiveTv::StatusJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool active = active_.load();
    const auto audio = application_.GetStreamPlaybackStatus();
    const char* state =
        !active                ? (last_error_.empty() ? "stopped" : "error")
        : reconnecting_.load() ? "reconnecting"
        : frames_.load() > 0 && application_.IsRadioActive() && audio.position_ms > 0 ? "playing"
                                                                                      : "starting";
    cJSON* value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "state", state);
    cJSON_AddBoolToObject(value, "active", active);
    cJSON_AddBoolToObject(value, "visible", active && display_.IsLiveTvVisible());
    cJSON_AddBoolToObject(value, "worker_running", task_.load() != nullptr);
    cJSON_AddStringToObject(value, "channel_id", channel_id_.c_str());
    cJSON_AddStringToObject(value, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(value, "title", title_.c_str());
    cJSON_AddStringToObject(value, "error", last_error_.c_str());
    cJSON_AddNumberToObject(value, "frames", frames_.load() ? display_.LiveTvRefreshCount() : 0);
    cJSON_AddNumberToObject(value, "submitted_frames", frames_.load());
    cJSON_AddNumberToObject(value, "audio_position_ms", active ? audio.position_ms : 0);
    cJSON_AddNumberToObject(value, "fps", fps_x100_.load() / 100.0);
    char* encoded = cJSON_PrintUnformatted(value);
    std::string result = encoded ? encoded : "{}";
    cJSON_free(encoded);
    cJSON_Delete(value);
    return result;
}

void RobotLiveTv::StopVideoOnly() { active_.store(false); }

void RobotLiveTv::TaskEntry(void* context) {
    auto* player = static_cast<RobotLiveTv*>(context);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (player->display_benchmark_)
        player->RunDisplayBenchmark();
    else
        player->Run();
    player->task_.store(nullptr);
    vTaskDeleteWithCaps(nullptr);
}

bool RobotLiveTv::StartDisplayBenchmark(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.load() || task_.load() != nullptr ||
        application_.GetDeviceState() != kDeviceStateIdle) {
        if (error != nullptr)
            *error = "stop TV/audio and return to idle before the display benchmark";
        return false;
    }
    display_benchmark_ = true;
    active_.store(true);
    application_.Schedule([this]() { display_.SetLiveTvActive(true, "TFT benchmark 12 fps"); });
    TaskHandle_t task = nullptr;
    if (xTaskCreateWithCaps(&TaskEntry, "live_tv", 8192, this, 2, &task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        active_.store(false);
        application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
        if (error != nullptr)
            *error = "could not create the display benchmark worker";
        return false;
    }
    task_.store(task);
    xTaskNotifyGive(task);
    return true;
}

void RobotLiveTv::RunDisplayBenchmark() {
    // Generated moving bars isolate LVGL/SPI from HTTP, JPEG and audio. This is
    // deliberately bounded and never changes pins, NVS or the audio pipeline.
    vTaskDelay(pdMS_TO_TICKS(100));
    FrameStats stats;
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t frame = 0;
         frame < 120 && active_.load() && application_.GetDeviceState() == kDeviceStateIdle;
         ++frame) {
        const int64_t started = esp_timer_get_time();
        constexpr size_t kPixels = 240 * 240;
        auto* pixels = static_cast<uint16_t*>(
            heap_caps_malloc(kPixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (pixels == nullptr)
            break;
        constexpr uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0x07ff, 0xffff, 0x0000};
        for (size_t y = 0; y < 240; ++y)
            for (size_t x = 0; x < 240; ++x)
                pixels[y * 240 + x] = colors[((x + frame * 4) / 40 + y / 80) % 6];
        auto image = std::make_unique<LvglAllocatedImage>(pixels, kPixels * 2, 240, 240, 240 * 2,
                                                          LV_COLOR_FORMAT_RGB565);
        const int64_t ready = esp_timer_get_time();
        const bool submitted = display_.SetLiveTvFrame(std::move(image));
        stats.Add(0, 0, ready - started, esp_timer_get_time() - ready, submitted, display_);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(83));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TFT benchmark finished: refreshed=%lu/120 (no HTTP/JPEG/audio)",
             static_cast<unsigned long>(display_.LiveTvRefreshCount()));
    active_.store(false);
    application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
}

void RobotLiveTv::Run() {
    std::string video_url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        video_url = video_url_;
    }

    auto jpeg =
        static_cast<uint8_t*>(heap_caps_malloc(kMaxJpegBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (jpeg == nullptr) {
        StopAfterFailure("not enough PSRAM for the bounded JPEG frame buffer");
        return;
    }

    bool radio_started = false;
    const TickType_t radio_wait_started = xTaskGetTickCount();
    uint32_t reconnect_delay_ms = kVideoReconnectInitialDelayMs;
    uint32_t reconnect_attempt = 0;
    std::unique_ptr<FrameStats> stats;

    while (active_.load()) {
        if (!application_.IsRadioActive()) {
            if (radio_started)
                break;
            if (xTaskGetTickCount() - radio_wait_started >= kRadioStartTimeout) {
                StopAfterFailure("radio audio did not start within 60 seconds");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        radio_started = true;
        if (!stats)
            stats = std::make_unique<FrameStats>();

        esp_http_client_config_t config = {};
        config.url = video_url.c_str();
        config.timeout_ms = kStreamTimeoutMs;
        config.buffer_size = kReadBufferBytes;
        config.buffer_size_tx = 1024;
        config.keep_alive_enable = true;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        auto client = esp_http_client_init(&config);
        if (client == nullptr) {
            StopAfterFailure("could not initialize video HTTP client");
            break;
        }

        const esp_err_t open_result = esp_http_client_open(client, 0);
        if (open_result != ESP_OK) {
            ESP_LOGW(TAG, "Video connection failed: %s", esp_err_to_name(open_result));
            esp_http_client_cleanup(client);
        } else {
            esp_http_client_fetch_headers(client);
            const int status = esp_http_client_get_status_code(client);
            if (status != 200) {
                ESP_LOGW(TAG, "Video endpoint returned HTTP %d; reconnecting", status);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
            } else {
                // Poll cancellation once per second. A stalled video connection
                // is retried below without interrupting the independent audio stream.
                esp_http_client_set_timeout_ms(client, 1000);
                HttpStreamReader reader(client, active_);
                size_t invalid_frames = 0;
                bool connection_had_frame = false;
                while (active_.load() && application_.IsRadioActive()) {
                    size_t jpeg_size = 0;
                    const int64_t read_started = esp_timer_get_time();
                    if (!ReadMjpegFrame(reader, jpeg, jpeg_size, active_))
                        break;
                    if (!IsBoundedJpeg(jpeg, jpeg_size)) {
                        StopAfterFailure("Video không phải JPEG baseline trong giới hạn 240×240.");
                        break;
                    }

                    uint8_t* pixels = nullptr;
                    size_t pixel_bytes = 0, width = 0, height = 0, stride = 0;
                    const int64_t decode_started = esp_timer_get_time();
                    const esp_err_t decode = jpeg_to_image(jpeg, jpeg_size, &pixels, &pixel_bytes,
                                                           &width, &height, &stride);
                    const int64_t decode_finished = esp_timer_get_time();
                    if (decode != ESP_OK || pixels == nullptr || width > 240 || height > 240 ||
                        pixel_bytes == 0) {
                        if (pixels != nullptr)
                            heap_caps_free(pixels);
                        if (++invalid_frames >= 8) {
                            StopAfterFailure(
                                "Không giải mã được 8 frame liên tiếp. Kiểm tra nguồn video.");
                            break;
                        }
                        continue;
                    }

                    invalid_frames = 0;
                    if (!connection_had_frame && reconnect_attempt > 0) {
                        ESP_LOGI(TAG, "Video stream recovered on reconnect attempt=%lu",
                                 static_cast<unsigned long>(reconnect_attempt));
                    }
                    connection_had_frame = true;
                    reconnecting_.store(false);
                    reconnect_attempt = 0;
                    auto image = std::make_unique<LvglAllocatedImage>(
                        pixels, pixel_bytes, static_cast<int>(width), static_cast<int>(height),
                        static_cast<int>(stride), LV_COLOR_FORMAT_RGB565);
                    if (!image->IsValid()) {
                        image.reset();
                        StopAfterFailure("could not wrap a decoded video frame for LVGL");
                        break;
                    }
                    const bool submitted = display_.SetLiveTvFrame(std::move(image));
                    if (submitted)
                        frames_.fetch_add(1);
                    stats->Add(jpeg_size, decode_started - read_started,
                               decode_finished - decode_started,
                               esp_timer_get_time() - decode_finished, submitted, display_);
                    fps_x100_.store(stats->FpsX100());
                }

                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                if (connection_had_frame)
                    reconnect_delay_ms = kVideoReconnectInitialDelayMs;
            }
        }

        if (!active_.load())
            break;
        if (!application_.IsRadioActive())
            break;

        ++reconnect_attempt;
        reconnecting_.store(true);
        if (reconnect_attempt >= 5) {
            StopAfterFailure(
                "Không khôi phục được video sau 5 lần kết nối. Hãy kiểm tra nguồn kênh.");
            break;
        }
        ESP_LOGW(TAG, "Video stream interrupted; reconnecting attempt=%lu in %lu ms",
                 static_cast<unsigned long>(reconnect_attempt),
                 static_cast<unsigned long>(reconnect_delay_ms));
        uint32_t remaining_ms = reconnect_delay_ms;
        while (active_.load() && remaining_ms > 0) {
            const uint32_t wait_ms = std::min<uint32_t>(remaining_ms, 100);
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            remaining_ms -= wait_ms;
        }
        reconnect_delay_ms = std::min<uint32_t>(reconnect_delay_ms * 2, kVideoReconnectMaxDelayMs);
    }

    heap_caps_free(jpeg);
    if (active_.load()) {
        if (radio_started && !application_.IsRadioActive()) {
            FinishAfterRadioStopped();
        } else if (!radio_started &&
                   xTaskGetTickCount() - radio_wait_started >= kRadioStartTimeout) {
            StopAfterFailure("radio audio did not start within 60 seconds");
        }
    }
}

void RobotLiveTv::StopAfterFailure(const char* reason) {
    if (!active_.exchange(false))
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = reason;
    }
    ESP_LOGW(TAG, "Live TV stopped: %s", reason);
    application_.RequestStopRadio();
    application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
}

void RobotLiveTv::FinishAfterRadioStopped() {
    if (!active_.exchange(false))
        return;
    ESP_LOGI(TAG, "Live TV video stopped because radio audio ended");
    application_.Schedule([this]() { display_.SetLiveTvActive(false, {}); });
}

}  // namespace kynangso
