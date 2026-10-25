#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "cjson_utils.h"
#include "display.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "settings.h"
#include "system_info.h"
#include "text_glyph_payload.h"
#include "websocket_protocol.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#define TAG "Application"

namespace {

constexpr int64_t kAudioVisualizerFrameIntervalUs = 40000;
constexpr size_t kMaxMediaSubtitleCount = 48;
constexpr size_t kMaxMediaSubtitleBytes = 96;

bool GetOptionalJsonString(const cJSON* object, const char* name, size_t max_bytes,
                           std::string* value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr ||
        std::strlen(item->valuestring) > max_bytes) {
        return false;
    }
    value->assign(item->valuestring);
    return true;
}

bool IsStreamPlaybackState(DeviceState state) {
    return state == kDeviceStateRadio || state == kDeviceStateNotifying;
}

}  // namespace

Application::Application()
    : notify_player_(audio_service_), continuous_stream_player_(audio_service_) {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {.callback =
                                                    [](void* arg) {
                                                        Application* app = (Application*)arg;
                                                        xEventGroupSetBits(app->event_group_,
                                                                           MAIN_EVENT_CLOCK_TICK);
                                                    },
                                                .arg = this,
                                                .dispatch_method = ESP_TIMER_TASK,
                                                .name = "clock_timer",
                                                .skip_unhandled_events = true};
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    esp_timer_create_args_t visualizer_timer_args = {
        .callback =
            [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                bool pending = false;
                {
                    std::lock_guard<std::mutex> lock(app->audio_visualizer_mutex_);
                    pending = app->audio_visualizer_frame_pending_;
                }
                if (pending) {
                    xEventGroupSetBits(app->event_group_, MAIN_EVENT_AUDIO_VISUALIZER);
                }
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_visualizer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&visualizer_timer_args, &audio_visualizer_timer_));
}

Application::~Application() {
    notify_player_.Stop();
    continuous_stream_player_.Stop();
    if (audio_visualizer_timer_ != nullptr) {
        esp_timer_stop(audio_visualizer_timer_);
        esp_timer_delete(audio_visualizer_timer_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (startup_wake_word_hint_timer_ != nullptr) {
        esp_timer_stop(startup_wake_word_hint_timer_);
        esp_timer_delete(startup_wake_word_hint_timer_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) { return state_machine_.TransitionTo(state); }

void Application::LoadBootAudioConfig() {
    const auto config = Board::GetInstance().GetBootAudioConfig();
    SetBootAudioConfig(config.enabled, config.volume);
}

void Application::SetBootAudioConfig(bool enabled, int volume) {
    boot_audio_enabled_ = enabled;
    boot_audio_volume_ = std::clamp(volume, 0, 100);
    if (!boot_audio_enabled_) {
        RestoreBootAudioVolume();
    }
}

void Application::ReportBootEvent(BootStage stage, const std::string& detail) {
    BootEvent event;
    event.stage = stage;
    std::snprintf(event.detail, sizeof(event.detail), "%s", detail.c_str());

    {
        std::lock_guard<std::mutex> lock(boot_event_mutex_);
        event.sequence = ++boot_event_sequence_;
        if (boot_event_ring_count_ == boot_event_ring_.size()) {
            boot_event_ring_head_ = (boot_event_ring_head_ + 1) % boot_event_ring_.size();
            --boot_event_ring_count_;
        }
        const size_t tail =
            (boot_event_ring_head_ + boot_event_ring_count_) % boot_event_ring_.size();
        boot_event_ring_[tail] = event;
        ++boot_event_ring_count_;
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_BOOT_EVENT);
}

void Application::EnsureBootAudioVolume() {
    if (!boot_audio_enabled_ || boot_audio_volume_active_) {
        return;
    }
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return;
    }
    boot_audio_restore_volume_ = codec->output_volume();
    codec->SetOutputVolume(boot_audio_volume_);
    boot_audio_volume_active_ = true;
}

void Application::RestoreBootAudioVolume() {
    if (!boot_audio_volume_active_) {
        return;
    }
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        codec->SetOutputVolume(boot_audio_restore_volume_);
    }
    boot_audio_volume_active_ = false;
}

void Application::PlayBootStageSound(const BootEvent& event) {
    if (!boot_audio_enabled_) {
        if (event.stage == BootStage::Ready || event.stage == BootStage::Error) {
            boot_sequence_active_ = false;
        }
        return;
    }
    if (!boot_sequence_active_ && event.stage != BootStage::Ready &&
        event.stage != BootStage::Error) {
        return;
    }

    const uint32_t stage_bit = 1U << static_cast<uint8_t>(event.stage);
    if ((boot_audio_stage_mask_ & stage_bit) != 0) {
        return;
    }

    const std::string_view sound = Board::GetInstance().GetBootSound(event.stage);

    boot_audio_stage_mask_ |= stage_bit;
    if (!sound.empty()) {
        EnsureBootAudioVolume();
        audio_service_.PlaySound(sound);
    }
    if (event.stage == BootStage::Ready || event.stage == BootStage::Error) {
        boot_sequence_active_ = false;
    }
}

void Application::HandleBootEvents() {
    while (true) {
        BootEvent event;
        {
            std::lock_guard<std::mutex> lock(boot_event_mutex_);
            if (boot_event_ring_count_ == 0) {
                break;
            }
            event = boot_event_ring_[boot_event_ring_head_];
            boot_event_ring_head_ = (boot_event_ring_head_ + 1) % boot_event_ring_.size();
            --boot_event_ring_count_;
        }

        auto& board = Board::GetInstance();
        auto* display = board.GetDisplay();
        if (display != nullptr) {
            display->OnBootEvent(event.stage, event.detail);
        }
        board.OnBootEvent(event);
        PlayBootStageSound(event);
    }
}

void Application::QueueAudioVisualizerFrame(const int16_t* pcm, size_t count, int sample_rate,
                                            uint32_t playback_id, AudioPlaybackSource source) {
    const auto state = GetDeviceState();
    const bool media = source == AudioPlaybackSource::Media &&
                       (state == kDeviceStateRadio || state == kDeviceStateNotifying);
    const bool speech =
        source == AudioPlaybackSource::Conversation && state == kDeviceStateSpeaking;
    if (pcm == nullptr || count == 0 || sample_rate <= 0 || (!media && !speech)) {
        return;
    }
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(audio_visualizer_mutex_);
        notify = !audio_visualizer_frame_pending_;
        const size_t copied = std::min(count, AudioPlaybackFrame::kSamples);
        // Keep a contiguous window: striding samples would alias the FFT.
        std::copy_n(pcm, copied, audio_visualizer_frame_.pcm.begin());
        audio_visualizer_frame_.sample_count = copied;
        audio_visualizer_frame_.sample_rate = sample_rate;
        audio_visualizer_frame_.playback_id = playback_id;
        audio_visualizer_frame_.source = source;
        audio_visualizer_frame_pending_ = true;
    }
    if (notify) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_AUDIO_VISUALIZER);
    }
}

void Application::HandleAudioVisualizerEvent() {
    // The timer callback only posts an event. Keeping timer operations here
    // serializes frame/drain races without holding the audio mailbox mutex.
    if (audio_visualizer_timer_armed_) {
        // An expired one-shot is already stopped (ESP_ERR_INVALID_STATE).
        esp_timer_stop(audio_visualizer_timer_);
        audio_visualizer_timer_armed_ = false;
    }
    AudioPlaybackFrame frame;
    uint32_t generation = 0;
    bool stopped = false;
    bool has_frame = false;
    int64_t delay_us = 0;
    {
        std::lock_guard<std::mutex> lock(audio_visualizer_mutex_);
        stopped = audio_visualizer_stop_pending_;
        audio_visualizer_stop_pending_ = false;
        generation = audio_visualizer_generation_;
        const int64_t now = esp_timer_get_time();
        if (audio_visualizer_frame_pending_ &&
            now - audio_visualizer_last_frame_us_ >= kAudioVisualizerFrameIntervalUs) {
            frame = audio_visualizer_frame_;
            audio_visualizer_frame_pending_ = false;
            audio_visualizer_last_frame_us_ = now;
            has_frame = true;
        } else if (audio_visualizer_frame_pending_) {
            delay_us = kAudioVisualizerFrameIntervalUs - (now - audio_visualizer_last_frame_us_);
        }
    }

    if (delay_us > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(audio_visualizer_timer_, delay_us));
        audio_visualizer_timer_armed_ = true;
        // A concurrent drain posts another event, which cancels this one-shot;
        // a concurrent frame overwrites the mailbox without extending the deadline.
    }

    auto* display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    // A new playback may already have published a frame after the stop. Apply
    // the stop first, then only that new generation's latest frame.
    if (stopped) {
        display->OnAudioPlaybackStopped();
    }
    if (has_frame) {
        {
            std::lock_guard<std::mutex> lock(audio_visualizer_mutex_);
            if (generation != audio_visualizer_generation_) {
                return;
            }
        }
        // Display hooks retain ownership of boot presentation blocking. A stop
        // arriving during this call is delivered on the next main-loop pass.
        // Recheck state/session on the main task; an old frame cannot reopen
        // media UI after Stop, or drive the mouth after listening has started.
        const auto state = GetDeviceState();
        if ((frame.source == AudioPlaybackSource::Media && state == kDeviceStateRadio &&
             frame.playback_id == radio_playback_id_) ||
            (frame.source == AudioPlaybackSource::Media && state == kDeviceStateNotifying &&
             frame.playback_id == notification_playback_id_) ||
            (frame.source == AudioPlaybackSource::Conversation && state == kDeviceStateSpeaking)) {
            display->OnAudioPlaybackFrame(frame);
        }
    }
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();
    ESP_LOGI(TAG, "After board/audio init");
    SystemInfo::PrintHeapStats();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    const bool visualize_audio = display != nullptr && display->SupportsAudioVisualization();
    callbacks.on_playback_drained = [this, visualize_audio]() {
        if (visualize_audio) {
            std::lock_guard<std::mutex> lock(audio_visualizer_mutex_);
            ++audio_visualizer_generation_;
            audio_visualizer_frame_pending_ = false;
            audio_visualizer_stop_pending_ = true;
        }
        xEventGroupSetBits(event_group_, MAIN_EVENT_PLAYBACK_DRAINED |
                                             (visualize_audio ? MAIN_EVENT_AUDIO_VISUALIZER : 0));
    };
    if (visualize_audio) {
        callbacks.on_playback_frame = [this](const int16_t* pcm, size_t count, int sample_rate,
                                             uint32_t playback_id, AudioPlaybackSource source) {
            QueueAudioVisualizerFrame(pcm, count, sample_rate, playback_id, source);
        };
    }
    callbacks.on_playback_progress = [this](uint32_t playback_id, uint32_t media_position_ms) {
        if (GetDeviceState() == kDeviceStateRadio) {
            continuous_stream_player_.OnPlaybackProgress(playback_id, media_position_ms);
        } else {
            notify_player_.OnPlaybackProgress(playback_id, media_position_ms);
            const auto second = static_cast<int32_t>(media_position_ms / 1000);
            if (last_media_progress_second_.exchange(second, std::memory_order_relaxed) != second) {
                const auto playback = notify_player_.GetPlaybackStatus();
                if (!playback.active || playback.metadata.duration_ms == 0) {
                    return;
                }

                bool expected = false;
                if (media_progress_update_queued_.compare_exchange_strong(
                        expected, true, std::memory_order_acq_rel)) {
                    Schedule([this]() {
                        const auto current = notify_player_.GetPlaybackStatus();
                        if (current.active && current.metadata.duration_ms > 0) {
                            auto* display = Board::GetInstance().GetDisplay();
                            if (display != nullptr) {
                                display->SetMediaPlaybackProgress(current.position_ms,
                                                                  current.metadata.duration_ms);
                            }
                        }
                        media_progress_update_queued_.store(false, std::memory_order_release);
                    });
                }
            }
        }
    };
    audio_service_.SetCallbacks(callbacks);

    LoadBootAudioConfig();
    display->StartBootSequence();
    board.OnApplicationReady();
    ReportBootEvent(BootStage::SystemInitializing);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    device_bridge_client_ = std::make_unique<DeviceBridgeClient>(
        [this](const std::string& payload, std::string* result, std::string* error) {
            return HandleDeviceBridgeCommand(payload, result, error);
        });

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();

        switch (event) {
            case NetworkEvent::Scanning:
                ReportBootEvent(BootStage::WifiScanning, "Scanning access points");
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                ReportBootEvent(BootStage::WifiConnecting, data);
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                ReportBootEvent(BootStage::WifiConnected, data);
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "warning",
                      Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "warning",
                      Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "warning",
                      Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS =
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO | MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE | MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED | MAIN_EVENT_NETWORK_DISCONNECTED | MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING | MAIN_EVENT_STOP_LISTENING | MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED | MAIN_EVENT_PLAYBACK_DRAINED | MAIN_EVENT_STARTUP_HINT_TIMEOUT |
        MAIN_EVENT_BOOT_EVENT | MAIN_EVENT_AUDIO_VISUALIZER;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            if (GetDeviceState() == kDeviceStateRadio) {
                StopRadio();
            } else if (GetDeviceState() == kDeviceStateNotifying) {
                StopNotification();
            }
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "cancel",
                  Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_PLAYBACK_DRAINED) {
            if (audio_service_.IsPlaybackIdle() && GetDeviceState() == kDeviceStateRadio) {
                continuous_stream_player_.OnPlaybackDrained();
            } else if (audio_service_.IsPlaybackIdle()) {
                notify_player_.OnPlaybackDrained();
            }
            // Deferred listening start (auto mode): the playback queue has
            // drained, so it is now safe to enable voice processing.
            if (pending_listening_start_ && GetDeviceState() == kDeviceStateListening &&
                audio_service_.IsPlaybackIdle()) {
                pending_listening_start_ = false;
                StartListeningAudio();
            }
            if (boot_audio_volume_active_ && !boot_sequence_active_ &&
                audio_service_.IsPlaybackIdle()) {
                RestoreBootAudioVolume();
            }
        }

        if (bits & MAIN_EVENT_BOOT_EVENT) {
            HandleBootEvents();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    // Drop the remaining packets. Leaving them in the queue would
                    // stall the Opus codec task (it waits for queue space), which in
                    // turn deadlocks the whole audio input pipeline, as no new
                    // MAIN_EVENT_SEND_AUDIO event would ever be triggered again.
                    while (audio_service_.PopPacketFromSendQueue())
                        ;
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            if (text_bridge_active_ && esp_timer_get_time() - text_bridge_started_us_ > 40000000 &&
                GetDeviceState() == kDeviceStateListening) {
                ESP_LOGW(TAG, "Web text bridge timed out waiting for the assistant");
                text_bridge_active_ = false;
                if (protocol_ && protocol_->IsAudioChannelOpened()) {
                    protocol_->CloseAudioChannel();
                } else {
                    SetDeviceState(kDeviceStateIdle);
                }
            }
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // A cancelled HTTP worker may take up to its read timeout to exit.
            // Retry a pending media command without blocking the main loop.
            if (pending_media_local_ || !pending_media_url_.empty()) {
                TryStartPendingMedia();
            }

            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
                // SystemInfo::PrintTaskList();
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
            }
        }
        if (bits & MAIN_EVENT_STARTUP_HINT_TIMEOUT) {
            HideStartupWakeWordHint();
        }
        if (bits & MAIN_EVENT_AUDIO_VISUALIZER) {
            HandleAudioVisualizerEvent();
        }
        ShowStartupWakeWordHint();
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    // WSS setup resolves/connects through lwIP. Start only after the board has
    // reported a usable network so the TCP/IP task and mailbox are ready.
    if (device_bridge_client_ != nullptr) {
        device_bridge_client_->Start();
    }

    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate(
            [](void* arg) {
                Application* app = static_cast<Application*>(arg);
                app->ActivationTask();
                app->activation_task_handle_ = nullptr;
                vTaskDelete(NULL);
            },
            "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (IsStreamPlaybackState(state)) {
        if (state == kDeviceStateRadio) {
            StopRadio();
        } else {
            StopNotification();
        }
    }
    if (state == kDeviceStateConnecting || state == kDeviceStateListening ||
        state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    // Protocol start may have already raised MAIN_EVENT_ERROR. Do not replace
    // that alert with the "ready" UI/sound — the main loop can process both
    // events back-to-back because the activation task is lower priority.
    const bool has_error = !last_error_message_.empty();
    if (!startup_completed_) {
        startup_completed_ = true;
        pending_startup_wake_word_hint_ = !has_error;
    }
    if (!has_error) {
        auto display = Board::GetInstance().GetDisplay();
        std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
        display->ShowNotification(message.c_str());
        ReportBootEvent(BootStage::Ready, "XiaoZhi ready");
    } else {
        ReportBootEvent(BootStage::Error, last_error_message_);
    }

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }

    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_download", Lang::Sounds::OGG_UPGRADE);

        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success =
            assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                Schedule([display, message = std::string(buffer)]() {
                    display->SetChatMessage("system", message.c_str());
                });
            });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "cancel",
                  Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10;  // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        ReportBootEvent(BootStage::CheckingVersion, "OTA/version service");
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        auto check = ota_->CheckVersion();
        if (!check) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            const auto& err = check.error();
            char error_message[160];
            int error_message_length =
                snprintf(error_message, sizeof(error_message), "%s", err.ToString().c_str());
            if (error_message_length < 0 ||
                error_message_length >= static_cast<int>(sizeof(error_message))) {
                snprintf(error_message, sizeof(error_message), "%s", err.Message());
            }

            char buffer[320];
            int alert_message_length =
                snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
                         retry_delay, error_message);
            if (alert_message_length < 0 ||
                alert_message_length >= static_cast<int>(sizeof(buffer))) {
                snprintf(buffer, sizeof(buffer), "%s", err.Message());
            }
            Alert(Lang::Strings::ERROR, buffer, "cloud_off", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay,
                     retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2;  // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10;  // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return;  // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    ReportBootEvent(BootStage::LoggingIn, "Connecting to XiaoZhi");
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        ReportBootEvent(BootStage::LoggingIn, "Transport connected");
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });

    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });

    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG,
                     "Server sample rate %d does not match device output sample rate %d, "
                     "resampling may cause distortion",
                     protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });

    protocol_->OnAudioChannelClosed([this]() {
        Schedule([this]() {
            text_bridge_active_ = false;
            // HTTP music/notification/radio playback is independent from the
            // conversation channel. Check on the main task: playback may start
            // after `goodbye` arrives but before this scheduled callback runs.
            if (IsStreamPlaybackState(GetDeviceState())) {
                ESP_LOGI(TAG, "Conversation channel closed; keeping HTTP audio playback");
                return;
            }
            Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
            TryStartPendingRadio();
        });
    });

    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Incoming JSON message has no type");
            return;
        }
        if (strcmp(type->valuestring, "radio.start") == 0 ||
            strcmp(type->valuestring, "radio.stop") == 0 ||
            strcmp(type->valuestring, "media.play") == 0 ||
            strcmp(type->valuestring, "media.stop") == 0) {
            std::string error;
            if (!HandleStreamPlaybackCommand(root, &error)) {
                ESP_LOGW(TAG, "Ignoring invalid stream playback command: %s", error.c_str());
            }
            return;
        }
        if (strcmp(type->valuestring, "notify") == 0) {
            auto audio_url = cJSON_GetObjectItem(root, "audio_url");
            if (!cJSON_IsString(audio_url) || audio_url->valuestring[0] == '\0') {
                ESP_LOGW(TAG, "Notify message requires audio_url");
                return;
            }

            std::vector<NotifySubtitle> subtitles;
            auto subtitles_json = cJSON_GetObjectItem(root, "subtitles");
            if (subtitles_json != nullptr && !cJSON_IsArray(subtitles_json)) {
                ESP_LOGW(TAG, "Notify subtitles must be an array");
                return;
            }
            if (cJSON_IsArray(subtitles_json)) {
                cJSON* item = nullptr;
                cJSON_ArrayForEach (item, subtitles_json) {
                    auto start_ms = cJSON_GetObjectItem(item, "start_ms");
                    auto text = cJSON_GetObjectItem(item, "text");
                    if (!cJSON_IsNumber(start_ms) || start_ms->valuedouble < 0 ||
                        start_ms->valuedouble > std::numeric_limits<uint32_t>::max() ||
                        !cJSON_IsString(text)) {
                        ESP_LOGW(TAG, "Ignoring invalid notify subtitle");
                        continue;
                    }
                    subtitles.push_back({.start_ms = static_cast<uint32_t>(start_ms->valuedouble),
                                         .text = text->valuestring});
                }
            }

            Schedule([this, url = std::string(audio_url->valuestring),
                      subtitles = std::move(subtitles)]() mutable {
                StartNotification(std::move(url), std::move(subtitles));
            });
        } else if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (!cJSON_IsString(state)) {
                return;
            }
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    text_bridge_active_ = false;
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    std::vector<TextGlyph> glyphs;
                    uint8_t bpp = 0;
                    if (!TextGlyphPayload::Parse(root, glyphs, bpp)) {
                        glyphs.clear();
                    }
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring),
                              glyphs = std::move(glyphs), bpp]() {
                        display->AddTextGlyphs(glyphs, bpp);
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                std::vector<TextGlyph> glyphs;
                uint8_t bpp = 0;
                if (!TextGlyphPayload::Parse(root, glyphs, bpp)) {
                    glyphs.clear();
                }
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring),
                          glyphs = std::move(glyphs), bpp]() {
                    display->AddTextGlyphs(glyphs, bpp);
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() { Reboot(); });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring,
                      Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            CJsonStringUniquePtr root_json(cJSON_PrintUnformatted(root));
            ESP_LOGI(TAG, "Received custom message: %s", root_json ? root_json.get() : "");
            if (cJSON_IsObject(payload)) {
                CJsonStringUniquePtr payload_json(cJSON_PrintUnformatted(payload));
                if (payload_json) {
                    Schedule([this, display, payload_str = std::string(payload_json.get())]() {
                        display->SetChatMessage("system", payload_str.c_str());
                    });
                }
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });

    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{
        {digit_sound{'0', Lang::Sounds::OGG_0}, digit_sound{'1', Lang::Sounds::OGG_1},
         digit_sound{'2', Lang::Sounds::OGG_2}, digit_sound{'3', Lang::Sounds::OGG_3},
         digit_sound{'4', Lang::Sounds::OGG_4}, digit_sound{'5', Lang::Sounds::OGG_5},
         digit_sound{'6', Lang::Sounds::OGG_6}, digit_sound{'7', Lang::Sounds::OGG_7},
         digit_sound{'8', Lang::Sounds::OGG_8}, digit_sound{'9', Lang::Sounds::OGG_9}}};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                               [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion,
                        const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    last_error_message_.clear();
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() { xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT); }

void Application::StartListening() { xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING); }

void Application::StopListening() { xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING); }

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();

    if (IsStreamPlaybackState(state)) {
        if (state == kDeviceStateRadio) {
            StopRadio();
        } else {
            StopNotification();
        }
        state = kDeviceStateIdle;
    }

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() { ContinueOpenAudioChannel(mode); });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            // Return to idle so the device is not stuck in the connecting
            // state (not every failure path reports a network error)
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();

    if (IsStreamPlaybackState(state)) {
        if (state == kDeviceStateRadio) {
            StopRadio();
        } else {
            StopNotification();
        }
        state = kDeviceStateIdle;
    }

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() { ContinueOpenAudioChannel(kListeningModeManualStop); });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();

    if (IsStreamPlaybackState(state)) {
        if (state == kDeviceStateRadio) {
            StopRadio();
        } else {
            StopNotification();
        }
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        BeginWakeWordInvoke(wake_word);
    } else if (IsStreamPlaybackState(state)) {
        if (state == kDeviceStateRadio) {
            StopRadio();
        } else {
            StopNotification();
        }
        BeginWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue())
            ;

        if (state == kDeviceStateListening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::BeginWakeWordInvoke(const std::string& wake_word) {
    // Must run in the main task with the device in idle state
    audio_service_.EncodeWakeWord();

    // Always pass through the connecting state, even if the audio channel is
    // already opened. ContinueWakeWordInvoke() rejects any other state, so
    // skipping this transition would silently drop the wake word invocation.
    if (!SetDeviceState(kDeviceStateConnecting)) {
        // Wake word detection was stopped by the detection itself; restore it
        // so the device does not become unresponsive to wake words.
        audio_service_.EnableWakeWordDetection(true);
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        // Schedule to let the state change be processed first (UI update),
        // then continue with OpenAudioChannel which may block for ~1 second
        Schedule([this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
        return;
    }
    // Channel already opened, continue directly
    ContinueWakeWordInvoke(wake_word);
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            // Return to idle so the device is not stuck in the connecting
            // state (not every failure path reports a network error), and
            // wake word detection is re-enabled by the idle state handler.
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    if (new_state != kDeviceStateIdle) {
        HideStartupWakeWordHint();
    }
    clock_ticks_ = 0;
    // Any state change invalidates a pending deferred listening start;
    // the Listening case below re-arms it when needed.
    pending_listening_start_ = false;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();

    // State changes must dismiss the previous audio presentation even when an
    // in-flight frame is discarded or the decoder has no more PCM to drain.
    if (display->SupportsAudioVisualization()) {
        display->OnAudioPlaybackStopped();
    }

    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            // Keep a just-raised network error visible. SetDeviceState(idle)
            // queues STATE_CHANGED after Alert(), and the idle handler would
            // otherwise wipe the status, emotion, and chat message.
            if (last_error_message_.empty()) {
                display->SetStatus(Lang::Strings::STANDBY);
                display->ClearChatMessages();  // Clear messages first
                display->SetEmotion(
                    "neutral");  // Then set emotion (wechat mode checks child count)
            }
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            TryStartPendingRadio();
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            boot_sequence_active_ = false;
            RestoreBootAudioVolume();
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            if (text_bridge_active_) {
                // The web request supplies text through MCP; do not capture microphone audio.
                pending_listening_start_ = false;
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(false);
                TryStartPendingRadio();
                break;
            }

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for the playback queue to drain before enabling
                // voice processing. This prevents audio truncation when STOP arrives
                // late due to network jitter. Instead of blocking the main loop here,
                // defer the start until MAIN_EVENT_PLAYBACK_DRAINED arrives.
                if (listening_mode_ == kListeningModeAutoStop && !audio_service_.IsPlaybackIdle()) {
                    pending_listening_start_ = true;
                } else {
                    StartListeningAudio();
                }
            } else {
                ConfigureWakeWordForListening();
            }
            TryStartPendingRadio();
            break;
        case kDeviceStateSpeaking:
            boot_sequence_active_ = false;
            RestoreBootAudioVolume();
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateRadio:
            display->SetStatus("Radio");
            // Entering the state can clear the previous display session. Publish
            // radio metadata after that cleanup, not before this deferred event.
            display->SetMediaMetadata(GetActiveRadioMetadata());
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            break;
        case kDeviceStateNotifying:
            display->SetStatus(Lang::Strings::SPEAKING);
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::ShowStartupWakeWordHint() {
    // A coalesced activation/state event can queue another idle UI cleanup.
    // Wait for it before adding the hint, or ClearChatMessages() would erase it.
    if (!pending_startup_wake_word_hint_ ||
        (xEventGroupGetBits(event_group_) & MAIN_EVENT_STATE_CHANGED)) {
        return;
    }
    pending_startup_wake_word_hint_ = false;
    const auto state = GetDeviceState();
    const bool wake_word_running = audio_service_.IsWakeWordRunning();
    if (state != kDeviceStateIdle || !last_error_message_.empty() || !wake_word_running) {
        ESP_LOGI(TAG, "Startup wake word hint skipped (state=%d, wake word running=%d, error=%d)",
                 static_cast<int>(state), wake_word_running, !last_error_message_.empty());
        return;
    }

    const auto wake_words = audio_service_.GetWakeWords();
    std::string words;
    for (auto it = wake_words.begin(); it != wake_words.end(); ++it) {
        if (it->empty() || std::find(wake_words.begin(), it, *it) != it) {
            continue;
        }
        if (!words.empty()) {
            words += " / ";
        }
        words += *it;
    }
    if (words.empty()) {
        ESP_LOGW(TAG, "Startup wake word hint skipped: no available wake words");
        return;
    }
    std::string message = Lang::Strings::WAKE_WORD_HINT;
    const auto placeholder = message.find("%s");
    if (placeholder == std::string::npos) {
        ESP_LOGE(TAG, "Startup wake word hint skipped: invalid message template");
        return;
    }
    message.replace(placeholder, 2, words);
    if (startup_wake_word_hint_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback =
                [](void* arg) {
                    auto app = static_cast<Application*>(arg);
                    xEventGroupSetBits(app->event_group_, MAIN_EVENT_STARTUP_HINT_TIMEOUT);
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "startup_hint",
            .skip_unhandled_events = false,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &startup_wake_word_hint_timer_));
    }
    ESP_LOGI(TAG, "Startup wake word hint: %s", message.c_str());
    Board::GetInstance().GetDisplay()->ShowStartupHint(message.c_str());
    startup_wake_word_hint_message_ = std::move(message);
    ESP_ERROR_CHECK(esp_timer_start_once(startup_wake_word_hint_timer_, 5000 * 1000));
}

void Application::HideStartupWakeWordHint() {
    if (startup_wake_word_hint_message_.empty()) {
        return;
    }
    // A conversation or alert may have replaced the hint while its timer was running.
    Board::GetInstance().GetDisplay()->HideStartupHint(startup_wake_word_hint_message_.c_str());
    startup_wake_word_hint_message_.clear();
}

void Application::StartListeningAudio() {
    // Runs in the main loop, either directly from HandleStateChangedEvent or
    // deferred via MAIN_EVENT_PLAYBACK_DRAINED once the playback queue drains.
    if (GetDeviceState() != kDeviceStateListening) {
        return;
    }

    // Send the start listening command
    protocol_->SendStartListening(listening_mode_);
    audio_service_.EnableVoiceProcessing(true);

    ConfigureWakeWordForListening();

    // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
    if (play_popup_on_listening_) {
        play_popup_on_listening_ = false;
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
    }
}

void Application::ConfigureWakeWordForListening() {
#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
    // Enable wake word detection in listening mode (configured via Kconfig)
    audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
    // Disable wake word detection in listening mode
    audio_service_.EnableWakeWordDetection(false);
#endif
}

void Application::StartNotification(std::string audio_url, std::vector<NotifySubtitle> subtitles) {
    StartFiniteAudio(std::move(audio_url), std::move(subtitles), AudioPlaybackSource::Notification,
                     {});
}

void Application::StartMedia(std::string audio_url, MediaPlaybackMetadata metadata,
                             std::vector<NotifySubtitle> subtitles,
                             NotifyPlayer::InputFactory input_factory,
                             NotifyPlayer::FinishedCallback finished_callback) {
    ESP_LOGI(TAG, "Media start title=%s artist=%s lyrics=%u", metadata.title.c_str(),
             metadata.artist.c_str(), static_cast<unsigned>(subtitles.size()));
    StartFiniteAudio(std::move(audio_url), std::move(subtitles), AudioPlaybackSource::Media,
                     std::move(metadata), std::move(input_factory), std::move(finished_callback));
}

void Application::StartFiniteAudio(std::string audio_url, std::vector<NotifySubtitle> subtitles,
                                   AudioPlaybackSource source, MediaPlaybackMetadata metadata,
                                   NotifyPlayer::InputFactory input_factory,
                                   NotifyPlayer::FinishedCallback finished_callback) {
    if (GetDeviceState() != kDeviceStateIdle || notify_player_.IsBusy() ||
        continuous_stream_player_.IsBusy()) {
        ESP_LOGW(TAG, "Ignoring notify message while device is busy");
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    audio_service_.ReleaseWakeWordResources();
    while (audio_service_.PopPacketFromSendQueue()) {
        // Discard microphone audio left over from a previous conversation.
    }

    if (!SetDeviceState(kDeviceStateNotifying)) {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        return;
    }

    audio_service_.ResetDecoder();
    uint32_t playback_id = ++notification_playback_id_;
    if (playback_id == 0) {
        playback_id = ++notification_playback_id_;
    }
    finite_audio_source_ = source;
    auto* display = board.GetDisplay();
    display->SetMediaMetadata(source == AudioPlaybackSource::Media ? metadata
                                                                   : MediaPlaybackMetadata{});
    if (source == AudioPlaybackSource::Notification) {
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
    }

    StreamPlaybackOptions options;
    options.source = source;

    bool started = notify_player_.Start(
        std::move(audio_url), std::move(subtitles), playback_id,
        [this, source](uint32_t id, const std::string& text) {
            Schedule([this, source, id, text]() {
                if (GetDeviceState() == kDeviceStateNotifying && notification_playback_id_ == id) {
                    auto* display = Board::GetInstance().GetDisplay();
                    if (source == AudioPlaybackSource::Media) {
                        display->SetMediaSubtitle(text.c_str());
                    } else {
                        display->SetChatMessage("assistant", text.c_str());
                    }
                }
            });
        },
        [this, finished_callback = std::move(finished_callback)](uint32_t id, bool success) {
            Schedule([this, id, success, finished_callback]() {
                HandleNotificationFinished(id, success);
                if (finished_callback)
                    finished_callback(id, success);
            });
        },
        options, std::move(input_factory),
        source == AudioPlaybackSource::Media ? metadata : MediaPlaybackMetadata{});

    if (!started) {
        ESP_LOGE(TAG, "Failed to start notification playback");
        StopNotification();
    }
}

void Application::RequestNotification(std::string audio_url,
                                      std::vector<NotifySubtitle> subtitles) {
    Schedule([this, audio_url = std::move(audio_url), subtitles = std::move(subtitles)]() mutable {
        StartNotification(std::move(audio_url), std::move(subtitles));
    });
}

void Application::RequestMedia(std::string audio_url, MediaPlaybackMetadata metadata,
                               std::vector<NotifySubtitle> subtitles) {
    Schedule([this, audio_url = std::move(audio_url), metadata = std::move(metadata),
              subtitles = std::move(subtitles)]() mutable {
        QueueMedia(std::move(audio_url), std::move(metadata), std::move(subtitles));
    });
}

void Application::RequestLocalMedia(MediaPlaybackMetadata metadata,
                                    NotifyPlayer::InputFactory input_factory,
                                    NotifyPlayer::FinishedCallback finished_callback) {
    if (!input_factory)
        return;
    Schedule([this, metadata = std::move(metadata), input_factory = std::move(input_factory),
              finished_callback = std::move(finished_callback)]() mutable {
        QueueMedia({}, std::move(metadata), {}, std::move(input_factory),
                   std::move(finished_callback));
    });
}

NotifyPlayer::PlaybackStatus Application::GetLocalMediaPlaybackStatus() const {
    auto status = notify_player_.GetPlaybackStatus();
    if (status.metadata.source_path.empty())
        status.active = false;
    return status;
}

bool Application::HandleStreamPlaybackCommand(const cJSON* root, std::string* error) {
    const auto reject = [error](const char* reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    };
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == nullptr) {
        return reject("command type is required");
    }

    if (strcmp(type->valuestring, "radio.start") == 0) {
        const cJSON* audio_url = cJSON_GetObjectItemCaseSensitive(root, "audio_url");
        if (!cJSON_IsString(audio_url) || audio_url->valuestring == nullptr ||
            audio_url->valuestring[0] == '\0' || std::strlen(audio_url->valuestring) > 767) {
            return reject("radio.start requires a valid audio_url");
        }
        MediaPlaybackMetadata metadata;
        const cJSON* metadata_json = cJSON_GetObjectItemCaseSensitive(root, "metadata");
        if (metadata_json != nullptr &&
            (!cJSON_IsObject(metadata_json) ||
             !GetOptionalJsonString(metadata_json, "title", 192, &metadata.title) ||
             !GetOptionalJsonString(metadata_json, "artist", 128, &metadata.artist) ||
             !GetOptionalJsonString(metadata_json, "station", 128, &metadata.station) ||
             !GetOptionalJsonString(metadata_json, "genre", 96, &metadata.genre) ||
             !GetOptionalJsonString(metadata_json, "country", 64, &metadata.country) ||
             !GetOptionalJsonString(metadata_json, "codec", 32, &metadata.codec) ||
             !GetOptionalJsonString(metadata_json, "frequency", 64, &metadata.frequency))) {
            return reject("radio metadata is invalid or too long");
        }
        if (metadata_json != nullptr) {
            const cJSON* bitrate = cJSON_GetObjectItemCaseSensitive(metadata_json, "bitrate_kbps");
            if (bitrate != nullptr) {
                if (!cJSON_IsNumber(bitrate) || !std::isfinite(bitrate->valuedouble) ||
                    std::floor(bitrate->valuedouble) != bitrate->valuedouble ||
                    bitrate->valuedouble < 0 || bitrate->valuedouble > 10000) {
                    return reject("radio bitrate is invalid");
                }
                metadata.bitrate_kbps = static_cast<uint32_t>(bitrate->valuedouble);
            }
        }
        RequestRadio(std::string(audio_url->valuestring), std::move(metadata));
        return true;
    }
    if (strcmp(type->valuestring, "radio.stop") == 0) {
        RequestStopRadio();
        return true;
    }
    if (strcmp(type->valuestring, "media.play") == 0) {
        const cJSON* audio_url = cJSON_GetObjectItemCaseSensitive(root, "audio_url");
        if (!cJSON_IsString(audio_url) || audio_url->valuestring == nullptr ||
            audio_url->valuestring[0] == '\0' || std::strlen(audio_url->valuestring) > 767) {
            return reject("media.play requires a valid audio_url");
        }
        MediaPlaybackMetadata metadata;
        const cJSON* metadata_json = cJSON_GetObjectItemCaseSensitive(root, "metadata");
        if (metadata_json != nullptr) {
            if (!cJSON_IsObject(metadata_json) ||
                !GetOptionalJsonString(metadata_json, "title", 192, &metadata.title) ||
                !GetOptionalJsonString(metadata_json, "artist", 128, &metadata.artist) ||
                !GetOptionalJsonString(metadata_json, "album", 128, &metadata.album) ||
                !GetOptionalJsonString(metadata_json, "station", 128, &metadata.station)) {
                return reject("media metadata is invalid or too long");
            }
            const cJSON* duration = cJSON_GetObjectItemCaseSensitive(metadata_json, "duration_ms");
            if (duration != nullptr) {
                if (!cJSON_IsNumber(duration) || !std::isfinite(duration->valuedouble) ||
                    std::floor(duration->valuedouble) != duration->valuedouble ||
                    duration->valuedouble < 0 ||
                    duration->valuedouble > std::numeric_limits<uint32_t>::max()) {
                    return reject("media duration is invalid");
                }
                metadata.duration_ms = static_cast<uint32_t>(duration->valuedouble);
            }
        }
        std::vector<NotifySubtitle> subtitles;
        const cJSON* subtitles_json = cJSON_GetObjectItemCaseSensitive(root, "subtitles");
        if (subtitles_json != nullptr && !cJSON_IsArray(subtitles_json)) {
            return reject("media subtitles must be an array");
        }
        if (cJSON_IsArray(subtitles_json)) {
            if (cJSON_GetArraySize(subtitles_json) > kMaxMediaSubtitleCount) {
                return reject("media subtitle count exceeds the limit");
            }
            cJSON* item = nullptr;
            cJSON_ArrayForEach (item, subtitles_json) {
                const cJSON* start_ms = cJSON_GetObjectItemCaseSensitive(item, "start_ms");
                const cJSON* text = cJSON_GetObjectItemCaseSensitive(item, "text");
                if (!cJSON_IsObject(item) || !cJSON_IsNumber(start_ms) ||
                    !std::isfinite(start_ms->valuedouble) ||
                    std::floor(start_ms->valuedouble) != start_ms->valuedouble ||
                    start_ms->valuedouble < 0 ||
                    start_ms->valuedouble > std::numeric_limits<uint32_t>::max() ||
                    !cJSON_IsString(text) || text->valuestring == nullptr ||
                    std::strlen(text->valuestring) > kMaxMediaSubtitleBytes) {
                    return reject("media subtitle is invalid or too long");
                }
                subtitles.push_back(
                    {static_cast<uint32_t>(start_ms->valuedouble), text->valuestring});
            }
        }
        RequestMedia(std::string(audio_url->valuestring), std::move(metadata),
                     std::move(subtitles));
        return true;
    }
    if (strcmp(type->valuestring, "media.stop") == 0) {
        RequestStopMedia();
        return true;
    }
    return reject("unsupported device bridge command");
}

bool Application::HandleDeviceBridgeCommand(const std::string& payload_json,
                                            std::string* result_json, std::string* error) {
    cJSON* root = cJSON_ParseWithLength(payload_json.data(), payload_json.size());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (error != nullptr) {
            *error = "command payload must be a JSON object";
        }
        return false;
    }

    bool accepted = false;
    cJSON* result = cJSON_CreateObject();
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring != nullptr &&
        strcmp(type->valuestring, "radio.status") == 0) {
        const bool active = IsRadioActive();
        const auto metadata = active ? GetActiveRadioMetadata() : MediaPlaybackMetadata{};
        cJSON_AddBoolToObject(result, "active", active);
        cJSON_AddStringToObject(result, "state",
                                DeviceStateMachine::GetStateName(GetDeviceState()));
        cJSON_AddStringToObject(result, "title", metadata.title.c_str());
        cJSON_AddStringToObject(result, "station", metadata.station.c_str());
        cJSON_AddStringToObject(result, "genre", metadata.genre.c_str());
        cJSON_AddStringToObject(result, "country", metadata.country.c_str());
        cJSON_AddStringToObject(result, "codec", metadata.codec.c_str());
        cJSON_AddStringToObject(result, "frequency", metadata.frequency.c_str());
        cJSON_AddNumberToObject(result, "bitrate_kbps", metadata.bitrate_kbps);
        accepted = true;
    } else {
        accepted = HandleStreamPlaybackCommand(root, error);
        if (accepted) {
            cJSON_AddStringToObject(result, "status", "queued");
        } else {
            std::string board_result;
            accepted =
                Board::GetInstance().HandleDeviceBridgeCommand(payload_json, &board_result, error);
            if (accepted) {
                cJSON_Delete(result);
                result = cJSON_Parse(board_result.c_str());
                if (!cJSON_IsObject(result)) {
                    cJSON_Delete(result);
                    result = cJSON_CreateObject();
                }
            }
        }
    }

    if (accepted && result_json != nullptr) {
        char* encoded = cJSON_PrintUnformatted(result);
        *result_json = encoded == nullptr ? "{}" : encoded;
        cJSON_free(encoded);
    }
    cJSON_Delete(result);
    cJSON_Delete(root);
    return accepted;
}

void Application::QueueMedia(std::string audio_url, MediaPlaybackMetadata metadata,
                             std::vector<NotifySubtitle> subtitles,
                             NotifyPlayer::InputFactory input_factory,
                             NotifyPlayer::FinishedCallback finished_callback) {
    pending_radio_url_.clear();
    pending_radio_metadata_ = {};
    pending_media_url_ = std::move(audio_url);
    pending_media_metadata_ = std::move(metadata);
    pending_media_subtitles_ = std::move(subtitles);
    pending_media_local_ = static_cast<bool>(input_factory);
    pending_media_input_ = std::move(input_factory);
    pending_media_finished_callback_ = std::move(finished_callback);

    if (GetDeviceState() == kDeviceStateRadio) {
        ESP_LOGI(TAG, "Replacing radio playback with requested media");
        StopRadio();
    } else if (GetDeviceState() == kDeviceStateNotifying &&
               finite_audio_source_ == AudioPlaybackSource::Media) {
        ESP_LOGI(TAG, "Replacing current media playback with a new request");
        StopNotification();
    }
    TryStartPendingMedia();
}

void Application::RequestStopNotification() {
    Schedule([this]() { StopNotification(); });
}

void Application::RequestRadio(std::string audio_url, MediaPlaybackMetadata metadata) {
    Schedule([this, audio_url = std::move(audio_url), metadata = std::move(metadata)]() mutable {
        pending_media_url_.clear();
        pending_media_metadata_ = {};
        pending_media_subtitles_.clear();
        pending_media_input_ = {};
        pending_media_finished_callback_ = {};
        pending_media_local_ = false;
        if (GetDeviceState() == kDeviceStateRadio || continuous_stream_player_.IsBusy()) {
            ESP_LOGI(TAG, "Ignoring duplicate radio start; radio is already active");
            return;
        }

        pending_radio_url_ = std::move(audio_url);
        pending_radio_metadata_ = std::move(metadata);
        ESP_LOGI(TAG, "Radio start queued while device is in %s",
                 DeviceStateMachine::GetStateName(GetDeviceState()));
        TryStartPendingRadio();
    });
}

void Application::RequestStopMedia() {
    Schedule([this]() {
        pending_media_url_.clear();
        pending_media_metadata_ = {};
        pending_media_subtitles_.clear();
        pending_media_input_ = {};
        pending_media_local_ = false;
        pending_media_finished_callback_ = {};
        if (GetDeviceState() == kDeviceStateNotifying &&
            finite_audio_source_ == AudioPlaybackSource::Media) {
            StopNotification();
        } else {
            ESP_LOGI(TAG, "Media stop cleared pending playback; no finite media is active");
        }
    });
}

void Application::RequestStopRadio() {
    Schedule([this]() {
        // A stop request also cancels a start that is waiting for the current
        // assistant conversation to finish.
        pending_radio_url_.clear();
        pending_radio_metadata_ = {};
        if (GetDeviceState() == kDeviceStateRadio || continuous_stream_player_.IsBusy()) {
            StopRadio();
        } else {
            ESP_LOGI(TAG, "Ignoring radio stop; radio is not active");
        }
    });
}

bool Application::IsRadioActive() const {
    return GetDeviceState() == kDeviceStateRadio && continuous_stream_player_.IsBusy();
}

MediaPlaybackMetadata Application::GetActiveRadioMetadata() const {
    std::lock_guard<std::mutex> lock(radio_metadata_mutex_);
    return active_radio_metadata_;
}

void Application::StopNotification() {
    notify_player_.Stop();
    finite_audio_source_ = AudioPlaybackSource::Notification;
    audio_service_.ResetDecoder();
    auto& board = Board::GetInstance();
    board.GetDisplay()->SetChatMessage("assistant", "");
    board.GetDisplay()->SetMediaMetadata({});
    board.GetDisplay()->SetMediaSubtitle("");
    board.GetDisplay()->OnAudioPlaybackStopped();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    if (GetDeviceState() == kDeviceStateNotifying) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleNotificationFinished(uint32_t playback_id, bool success) {
    if (GetDeviceState() != kDeviceStateNotifying || notification_playback_id_ != playback_id) {
        return;
    }
    ESP_LOGI(TAG, "Notification playback %lu %s", static_cast<unsigned long>(playback_id),
             success ? "completed" : "failed");
    StopNotification();
}

void Application::TryStartPendingRadio() {
    // Music and radio share the same one-slot pending command. Media wins if
    // one is present; request handlers clear the older command when replaced.
    if (pending_media_local_ || !pending_media_url_.empty()) {
        TryStartPendingMedia();
        if (pending_media_local_ || !pending_media_url_.empty() ||
            GetDeviceState() != kDeviceStateIdle) {
            return;
        }
    }
    if (pending_radio_url_.empty()) {
        return;
    }

    const auto state = GetDeviceState();
    if (state == kDeviceStateListening && protocol_ && protocol_->IsAudioChannelOpened()) {
        // A listening conversation has no assistant audio left to preserve.
        // Close it now so the channel-close callback can move us to idle and
        // start the pending radio request.
        ESP_LOGI(TAG, "Closing listening conversation before starting pending radio");
        protocol_->CloseAudioChannel();
        return;
    }

    if (state == kDeviceStateListening) {
        // Some web/text conversations have already closed their MQTT channel
        // before the state callback reaches here. In that case there will be
        // no channel-close callback left to advance the state, so transition
        // explicitly before starting the independent radio session.
        ESP_LOGI(TAG, "Ending listening conversation before starting pending radio");
        if (!SetDeviceState(kDeviceStateIdle)) {
            return;
        }
    }

    if (GetDeviceState() != kDeviceStateIdle) {
        return;
    }

    auto audio_url = std::move(pending_radio_url_);
    pending_radio_url_.clear();
    auto metadata = std::move(pending_radio_metadata_);
    pending_radio_metadata_ = {};
    StartRadio(std::move(audio_url), std::move(metadata));
}

void Application::TryStartPendingMedia() {
    if (!pending_media_local_ && pending_media_url_.empty()) {
        return;
    }

    const auto state = GetDeviceState();
    if (state == kDeviceStateListening && protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing listening conversation before starting pending media");
        protocol_->CloseAudioChannel();
        return;
    }

    if (state == kDeviceStateListening) {
        ESP_LOGI(TAG, "Ending listening conversation before starting pending media");
        if (!SetDeviceState(kDeviceStateIdle)) {
            return;
        }
    }

    if (GetDeviceState() != kDeviceStateIdle) {
        return;
    }
    if (notify_player_.IsBusy() || continuous_stream_player_.IsBusy()) {
        return;
    }

    auto audio_url = std::move(pending_media_url_);
    pending_media_url_.clear();
    auto metadata = std::move(pending_media_metadata_);
    pending_media_metadata_ = {};
    auto subtitles = std::move(pending_media_subtitles_);
    pending_media_subtitles_.clear();
    auto input_factory = std::move(pending_media_input_);
    pending_media_input_ = {};
    auto finished_callback = std::move(pending_media_finished_callback_);
    pending_media_finished_callback_ = {};
    pending_media_local_ = false;
    StartMedia(std::move(audio_url), std::move(metadata), std::move(subtitles),
               std::move(input_factory), std::move(finished_callback));
}

void Application::StartRadio(std::string audio_url, MediaPlaybackMetadata metadata) {
    if (GetDeviceState() != kDeviceStateIdle || notify_player_.IsBusy() ||
        continuous_stream_player_.IsBusy()) {
        ESP_LOGW(TAG, "Delaying radio start while device is busy (%s)",
                 DeviceStateMachine::GetStateName(GetDeviceState()));
        pending_radio_url_ = std::move(audio_url);
        pending_radio_metadata_ = std::move(metadata);
        return;
    }

    auto& board = Board::GetInstance();
    // Radio is a separate playback mode. Close any leftover XiaoZhi
    // conversation channel before switching modes so delayed MQTT audio
    // packets cannot be mistaken for the next conversation.
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    audio_service_.ReleaseWakeWordResources();
    while (audio_service_.PopPacketFromSendQueue()) {
    }

    if (!SetDeviceState(kDeviceStateRadio)) {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        return;
    }

    ESP_LOGI(TAG, "Radio start station=%s", metadata.station.c_str());
    {
        std::lock_guard<std::mutex> lock(radio_metadata_mutex_);
        active_radio_metadata_ = metadata;
    }
    audio_service_.ResetDecoder();
    uint32_t playback_id = ++radio_playback_id_;
    if (playback_id == 0) {
        playback_id = ++radio_playback_id_;
    }

    bool started = continuous_stream_player_.Start(
        std::move(audio_url), playback_id, [this](uint32_t id, bool success) {
            Schedule([this, id, success]() { HandleRadioFinished(id, success); });
        });
    if (!started) {
        ESP_LOGE(TAG, "Failed to start radio playback");
        StopRadio();
    }
}

void Application::StopRadio() {
    pending_radio_url_.clear();
    pending_radio_metadata_ = {};
    {
        std::lock_guard<std::mutex> lock(radio_metadata_mutex_);
        active_radio_metadata_ = {};
    }
    if (GetDeviceState() != kDeviceStateRadio && !continuous_stream_player_.IsBusy()) {
        return;
    }

    continuous_stream_player_.Stop();
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    audio_service_.ResetDecoder();
    Board::GetInstance().GetDisplay()->SetMediaMetadata({});
    Board::GetInstance().GetDisplay()->SetMediaSubtitle("");
    Board::GetInstance().GetDisplay()->OnAudioPlaybackStopped();
    Board::GetInstance().GetDisplay()->SetChatMessage("assistant", "");
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    if (GetDeviceState() == kDeviceStateRadio) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleRadioFinished(uint32_t playback_id, bool success) {
    if (GetDeviceState() != kDeviceStateRadio || radio_playback_id_ != playback_id) {
        return;
    }
    ESP_LOGI(TAG, "Radio playback %lu %s", static_cast<unsigned long>(playback_id),
             success ? "completed" : "failed");
    StopRadio();
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    if (GetDeviceState() == kDeviceStateRadio) {
        StopRadio();
    } else if (GetDeviceState() == kDeviceStateNotifying) {
        StopNotification();
    }
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    if (GetDeviceState() == kDeviceStateRadio) {
        StopRadio();
    } else if (GetDeviceState() == kDeviceStateNotifying) {
        StopNotification();
    }

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
          Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG,
                 "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start();                              // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);  // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "cancel",
              Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();

    if (state == kDeviceStateIdle) {
        // May be called from outside the main task (e.g. board button
        // callbacks), so schedule the invocation instead of running it here
        Schedule([this, wake_word]() {
            if (GetDeviceState() == kDeviceStateIdle) {
                BeginWakeWordInvoke(wake_word);
            }
        });
    } else if (IsStreamPlaybackState(state)) {
        Schedule([this, wake_word]() {
            if (GetDeviceState() == kDeviceStateRadio) {
                StopRadio();
                BeginWakeWordInvoke(wake_word);
            } else if (GetDeviceState() == kDeviceStateNotifying) {
                StopNotification();
                BeginWakeWordInvoke(wake_word);
            }
        });
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (state == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::StartTextBridgeConversation(std::function<void(bool)> on_ready) {
    if (event_group_ == nullptr || GetDeviceState() != kDeviceStateIdle) {
        return false;
    }
    Schedule([this, on_ready = std::move(on_ready)]() {
        if (GetDeviceState() != kDeviceStateIdle || protocol_ == nullptr) {
            on_ready(false);
            return;
        }
        text_bridge_active_ = true;
        text_bridge_started_us_ = esp_timer_get_time();
        const ListeningMode previous_mode = listening_mode_;
        if (!SetDeviceState(kDeviceStateConnecting)) {
            text_bridge_active_ = false;
            on_ready(false);
            return;
        }
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
            text_bridge_active_ = false;
            listening_mode_ = previous_mode;
            SetDeviceState(kDeviceStateIdle);
            on_ready(false);
            return;
        }
        listening_mode_ = kListeningModeManualStop;
        pending_listening_start_ = false;
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        while (audio_service_.PopPacketFromSendQueue()) {
        }
        if (!SetDeviceState(kDeviceStateListening)) {
            text_bridge_active_ = false;
            listening_mode_ = previous_mode;
            protocol_->CloseAudioChannel();
            on_ready(false);
            return;
        }
        protocol_->SendStartListening(listening_mode_);
        if (!protocol_->PrimeAudioChannel()) {
            text_bridge_active_ = false;
            listening_mode_ = previous_mode;
            protocol_->CloseAudioChannel();
            on_ready(false);
            return;
        }
        // Only a short trigger travels in listen/detect. The actual text stays on the device
        // until the assistant requests it via the board's MCP tool.
        protocol_->SendWakeWordDetected("web_chat");
        on_ready(true);
    });
    return true;
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback) {
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
            case kAecOff:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                break;
            case kAecOnServerSide:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            case kAecOnDeviceSide:
                audio_service_.EnableDeviceAec(true);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) { audio_service_.PlaySound(sound); }

void Application::ResetProtocol() {
    Schedule([this]() {
        if (GetDeviceState() == kDeviceStateRadio) {
            StopRadio();
        } else if (GetDeviceState() == kDeviceStateNotifying) {
            StopNotification();
        }
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}
