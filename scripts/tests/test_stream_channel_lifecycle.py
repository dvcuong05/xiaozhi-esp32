"""Replay the production conversation-close callback against HTTP playback."""

import textwrap
import unittest

import test_startup_wake_word_hint as host


class StreamChannelLifecycleTests(unittest.TestCase):
    compile_and_run = host.StartupWakeWordHintTests.compile_and_run

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
