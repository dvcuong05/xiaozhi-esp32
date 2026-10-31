#include "web_chat_bridge.h"

#include "application.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <utility>

namespace kynangso {

void WebChatBridge::Expire(State& state) {
    if ((state.phase == Phase::Queued || state.phase == Phase::TriggerSent) &&
        esp_timer_get_time() - state.started_us > 30000000) {
        state.pending.clear();
        state.phase = Phase::Timeout;
    }
}

bool WebChatBridge::Queue(std::string prompt) {
    if (prompt.empty() || prompt.size() > 1000 || prompt.find('\0') != std::string::npos) {
        return false;
    }
    uint32_t id;
    const auto state = state_;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        Expire(*state);
        if (!state->pending.empty())
            return false;
        state->pending = std::move(prompt);
        state->phase = Phase::Queued;
        state->started_us = esp_timer_get_time();
        id = ++state->id;
    }
    const bool queued =
        Application::GetInstance().StartTextBridgeConversation([state, id](bool ready) {
            std::lock_guard<std::mutex> lock(state->mutex);
            Expire(*state);
            if (id != state->id || state->phase != Phase::Queued)
                return;
            state->phase = ready ? Phase::TriggerSent : Phase::Failed;
            if (!ready)
                state->pending.clear();
        });
    if (!queued) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (id == state->id) {
            state->pending.clear();
            state->phase = Phase::Failed;
        }
    }
    return queued;
}

const char* WebChatBridge::Status() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    Expire(*state_);
    switch (state_->phase) {
        case Phase::Idle:
            return "idle";
        case Phase::Queued:
            return "queued";
        case Phase::TriggerSent:
            return "trigger_sent";
        case Phase::Consumed:
            return "consumed";
        case Phase::Failed:
            return "failed";
        case Phase::Timeout:
            return "timeout";
    }
    return "failed";
}

std::expected<std::string, std::string> WebChatBridge::Consume() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    Expire(*state_);
    if (state_->pending.empty())
        return std::unexpected("No pending Web UI prompt");
    std::string prompt = std::move(state_->pending);
    state_->pending.clear();
    state_->phase = Phase::Consumed;
    ESP_LOGI("RobotWebChat", "Web prompt consumed by assistant tool (%u bytes)",
             static_cast<unsigned>(prompt.size()));
    return prompt;
}

}  // namespace kynangso
