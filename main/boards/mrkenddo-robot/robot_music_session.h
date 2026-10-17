#ifndef MRKENDDO_ROBOT_MUSIC_SESSION_H_
#define MRKENDDO_ROBOT_MUSIC_SESSION_H_

#include <string>
#include <string_view>

namespace mrkenddo {

// Tenclass sends server-tool progress and lyrics over the same sentence_start
// stream as speech. Keep this backend-specific presentation policy board-local.
class RobotMusicSession {
public:
    enum class Update { None, Started, Text, Stopped };
    Update OnAssistantText(std::string_view text);
    void Reset();
    bool IsActive() const { return active_; }
    const std::string& Title() const { return title_; }
    const std::string& CurrentLine() const { return current_; }
    const std::string& PreviousLine() const { return previous_; }

private:
    bool active_ = false;
    std::string title_;
    std::string current_;
    std::string previous_;
    static std::string ClipUtf8(std::string_view text, size_t limit);
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_MUSIC_SESSION_H_
