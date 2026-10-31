#ifndef KYNANGSO_WEB_CHAT_BRIDGE_H_
#define KYNANGSO_WEB_CHAT_BRIDGE_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>

namespace kynangso {

// One pending text message shared by the HTTP input and MCP consume tool.
// Callbacks retain only the state, never the board or an HTTP request.
class WebChatBridge {
public:
    bool Queue(std::string prompt);
    const char* Status();
    std::expected<std::string, std::string> Consume();

private:
    enum class Phase { Idle, Queued, TriggerSent, Consumed, Failed, Timeout };
    struct State {
        std::mutex mutex;
        std::string pending;
        Phase phase = Phase::Idle;
        int64_t started_us = 0;
        uint32_t id = 0;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    static void Expire(State& state);
};

}  // namespace kynangso

#endif  // KYNANGSO_WEB_CHAT_BRIDGE_H_
