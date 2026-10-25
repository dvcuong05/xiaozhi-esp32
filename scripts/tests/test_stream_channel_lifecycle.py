"""Replay production state callbacks and conversation cleanup during HTTP playback."""

import textwrap
import unittest

import test_startup_wake_word_hint as host


class StreamChannelLifecycleTests(unittest.TestCase):
    compile_and_run = host.StartupWakeWordHintTests.compile_and_run

    def test_radio_metadata_survives_deferred_state_entry(self):
        source = (host.ROOT / "main/application.cc").read_text(encoding="utf-8")
        methods = "\n".join(host.definition(source, signature) for signature in (
            "void Application::StartRadio(",
            "void Application::StopRadio()",
            "void Application::HandleStateChangedEvent()",
            "MediaPlaybackMetadata Application::GetActiveRadioMetadata() const",
        ))
        metadata = (host.ROOT / "main/media/media_metadata.h").read_text(encoding="utf-8")
        driver = r'''
            #include <cassert>
            #include <functional>
            #include <mutex>
            #include <string>
            #include <utility>
            #define ESP_LOGI(...) ((void)0)
            #define ESP_LOGW(...) ((void)0)
            #define ESP_LOGE(...) ((void)0)
            METADATA
            enum DeviceState { kDeviceStateUnknown, kDeviceStateIdle,
                kDeviceStateConnecting, kDeviceStateListening, kDeviceStateSpeaking,
                kDeviceStateRadio, kDeviceStateNotifying, kDeviceStateWifiConfiguring };
            enum class PowerSaveLevel { LOW_POWER, PERFORMANCE };
            enum ListeningMode { kListeningModeAutoStop, kListeningModeRealtime };
            namespace Lang::Strings {
                constexpr auto STANDBY = "idle", CONNECTING = "connecting",
                    LISTENING = "listening", SPEAKING = "speaking";
            }
            struct Display {
                MediaPlaybackMetadata metadata;
                bool SupportsAudioVisualization() const { return true; }
                void OnAudioPlaybackStopped() {}
                void SetStatus(const char* status) {
                    // The robot display clears the old session on non-speech status.
                    if (std::string(status) != Lang::Strings::SPEAKING) metadata = {};
                }
                void SetMediaMetadata(const MediaPlaybackMetadata& value) { metadata = value; }
                void SetMediaSubtitle(const char*) {}
                void ClearChatMessages() {}
                void SetChatMessage(const char*, const char*) {}
                void SetEmotion(const char*) {}
            };
            struct Led { void OnStateChanged() {} };
            struct Board {
                Display display;
                Led led;
                static Board& GetInstance() { static Board board; return board; }
                Display* GetDisplay() { return &display; }
                Led* GetLed() { return &led; }
                void SetPowerSaveLevel(PowerSaveLevel) {}
            };
            struct Protocol {
                bool open = true;
                bool IsAudioChannelOpened() const { return open; }
                void CloseAudioChannel() { open = false; }
            };
            struct AudioService {
                void EnableVoiceProcessing(bool) {}
                void EnableWakeWordDetection(bool) {}
                bool IsAfeWakeWord() const { return false; }
                void ReleaseWakeWordResources() {}
                bool PopPacketFromSendQueue() { return false; }
                void ResetDecoder() {}
                bool IsAudioProcessorRunning() const { return false; }
                bool IsPlaybackIdle() const { return true; }
            };
            struct NotifyPlayer { bool IsBusy() const { return false; } };
            struct StreamPlayer {
                bool allow_start = true, busy = false;
                bool IsBusy() const { return busy; }
                bool Start(std::string, uint32_t, std::function<void(uint32_t, bool)>) {
                    busy = allow_start;
                    return busy;
                }
                void Stop() { busy = false; }
            };
            struct DeviceStateMachine {
                DeviceState state = kDeviceStateIdle;
                DeviceState GetState() const { return state; }
                static const char* GetStateName(DeviceState) { return "host"; }
            };
            struct Application {
                DeviceStateMachine state_machine_;
                Protocol protocol;
                Protocol* protocol_ = &protocol;
                AudioService audio_service_;
                NotifyPlayer notify_player_;
                StreamPlayer continuous_stream_player_;
                mutable std::mutex radio_metadata_mutex_;
                MediaPlaybackMetadata active_radio_metadata_, pending_radio_metadata_;
                std::string pending_radio_url_, last_error_message_;
                uint32_t radio_playback_id_ = 0, clock_ticks_ = 0;
                bool pending_listening_start_ = false, text_bridge_active_ = false,
                    boot_sequence_active_ = false, play_popup_on_listening_ = false,
                    state_event_pending = false;
                ListeningMode listening_mode_ = kListeningModeAutoStop;
                DeviceState GetDeviceState() const { return state_machine_.GetState(); }
                bool SetDeviceState(DeviceState state) {
                    state_machine_.state = state;
                    state_event_pending = true; // Dispatch after StartRadio returns.
                    return true;
                }
                void HideStartupWakeWordHint() {}
                void RestoreBootAudioVolume() {}
                void StartListeningAudio() {}
                void ConfigureWakeWordForListening() {}
                void TryStartPendingRadio() {}
                void Schedule(std::function<void()>) {}
                void HandleRadioFinished(uint32_t, bool) {}
                void StartRadio(std::string, MediaPlaybackMetadata);
                void StopRadio();
                void HandleStateChangedEvent();
                MediaPlaybackMetadata GetActiveRadioMetadata() const;
                void DispatchStateEvent() {
                    assert(state_event_pending);
                    state_event_pending = false;
                    HandleStateChangedEvent();
                }
            };
            METHODS
            int main() {
                MediaPlaybackMetadata station;
                station.title = "VOV stream";
                station.station = "VOV Giao thong Ha Noi";
                station.genre = "news";
                station.country = "Vietnam";
                station.codec = "Opus";
                station.bitrate_kbps = 48;
                for (bool queued_during_speech : {false, true}) {
                    Board::GetInstance() = Board{};
                    Application app;
                    if (queued_during_speech) {
                        app.state_machine_.state = kDeviceStateSpeaking;
                        app.StartRadio("http://radio", station);
                        assert(!app.continuous_stream_player_.IsBusy());
                        assert(app.pending_radio_metadata_.station == station.station);
                        station = std::move(app.pending_radio_metadata_);
                        app.pending_radio_url_.clear();
                        app.state_machine_.state = kDeviceStateIdle;
                    }
                    app.StartRadio("http://radio", station);
                    assert(app.GetDeviceState() == kDeviceStateRadio);
                    assert(app.continuous_stream_player_.IsBusy());
                    app.DispatchStateEvent(); // Used to erase the metadata applied by StartRadio.
                    auto& display = Board::GetInstance().display;
                    assert(display.metadata.station == station.station);
                    assert(display.metadata.genre == "news");
                    assert(display.metadata.codec == "Opus");
                    assert(display.metadata.bitrate_kbps == 48);
                    app.HandleStateChangedEvent(); // Repeated state refresh remains safe.
                    assert(display.metadata.station == station.station);
                    app.StopRadio();
                    app.DispatchStateEvent();
                    assert(app.GetDeviceState() == kDeviceStateIdle);
                    assert(display.metadata.station.empty());
                    assert(app.GetActiveRadioMetadata().station.empty());
                }
                // Failed start coalesces radio -> idle before either state event runs.
                Application failed;
                failed.continuous_stream_player_.allow_start = false;
                failed.StartRadio("http://radio", station);
                failed.DispatchStateEvent();
                assert(failed.GetDeviceState() == kDeviceStateIdle);
                assert(Board::GetInstance().display.metadata.station.empty());
                assert(failed.GetActiveRadioMetadata().station.empty());
            }
        '''
        self.compile_and_run(textwrap.dedent(driver).replace("METADATA", metadata)
                             .replace("METHODS", methods))

    def test_delayed_conversation_close_preserves_http_playback(self):
        source = (host.ROOT / "main/application.cc").read_text(encoding="utf-8")
        callback = host.definition(source, "protocol_->OnAudioChannelClosed(") + ");"
        helper = host.definition(source, "bool IsStreamPlaybackState(")
        driver = r'''
            #include <cassert>
            #include <functional>
            #include <vector>
            #define ESP_LOGI(...) ((void)0)
            enum DeviceState { kDeviceStateIdle, kDeviceStateListening,
                               kDeviceStateRadio, kDeviceStateNotifying };
            enum class PowerSaveLevel { LOW_POWER };
            struct Display {
                int cleared = 0;
                void SetChatMessage(const char*, const char*) { ++cleared; }
            };
            struct Board {
                Display display;
                int power_changes = 0;
                static Board& GetInstance() { static Board board; return board; }
                Display* GetDisplay() { return &display; }
                void SetPowerSaveLevel(PowerSaveLevel) { ++power_changes; }
            };
            struct Protocol {
                std::function<void()> closed;
                void OnAudioChannelClosed(std::function<void()> cb) { closed = cb; }
            };
            HELPER
            struct Application {
                Protocol protocol;
                Protocol* protocol_ = &protocol;
                bool text_bridge_active_ = true;
                DeviceState state = kDeviceStateListening;
                std::vector<std::function<void()>> queued;
                int pending_checks = 0;
                DeviceState GetDeviceState() { return state; }
                void SetDeviceState(DeviceState value) { state = value; }
                void Schedule(std::function<void()> cb) { queued.push_back(cb); }
                void TryStartPendingRadio() { ++pending_checks; }
                void Configure() { CALLBACK }
                void Flush() {
                    auto callbacks = std::move(queued);
                    for (auto& cb : callbacks) cb();
                }
            };
            int main() {
                for (auto state : {kDeviceStateRadio, kDeviceStateNotifying}) {
                    for (bool delayed_start : {false, true}) {
                        Board::GetInstance() = Board{};
                        Application app;
                        app.Configure();
                        app.state = delayed_start ? kDeviceStateListening : state;
                        app.protocol.closed();
                        assert(Board::GetInstance().power_changes == 0);
                        app.state = state; // HTTP playback wins before main task runs.
                        app.Flush();
                        assert(app.state == state);
                        assert(!app.text_bridge_active_);
                        assert(app.pending_checks == 0);
                        assert(Board::GetInstance().power_changes == 0);
                        assert(Board::GetInstance().display.cleared == 0);
                    }
                }
                // Ordinary conversation close still returns idle and starts queued media.
                // If HTTP audio stopped before cleanup, it must not suppress cleanup either.
                for (auto state : {kDeviceStateListening, kDeviceStateNotifying}) {
                    Board::GetInstance() = Board{};
                    Application app;
                    app.Configure();
                    app.state = state;
                    app.protocol.closed();
                    if (state == kDeviceStateNotifying) app.state = kDeviceStateIdle;
                    app.Flush();
                    assert(app.state == kDeviceStateIdle);
                    assert(!app.text_bridge_active_);
                    assert(app.pending_checks == 1);
                    assert(Board::GetInstance().power_changes == 1);
                    assert(Board::GetInstance().display.cleared == 1);
                }
            }
        '''
        self.compile_and_run(textwrap.dedent(driver).replace("HELPER", helper)
                             .replace("CALLBACK", callback))
