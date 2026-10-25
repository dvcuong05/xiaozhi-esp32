#ifndef MRKENDDO_ROBOT_MUSIC_SESSION_H_
#define MRKENDDO_ROBOT_MUSIC_SESSION_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace mrkenddo {

// Tenclass sends server-tool progress and lyrics over the same sentence_start
// stream as speech. Keep this backend-specific presentation policy board-local.
class RobotMusicSession {
public:
    enum class Update { None, Started, Text, Stopped };
    Update OnAssistantText(std::string_view text);
    void SetMediaMetadata(std::string_view title, std::string_view artist, std::string_view station,
                          uint32_t duration_ms);
    void SetSubtitle(std::string_view text);
    void Reset();
    bool IsActive() const { return active_; }
    const std::string& Title() const { return title_; }
    const std::string& Artist() const { return artist_; }
    const std::string& Station() const { return station_; }
    uint32_t DurationMs() const { return duration_ms_; }
    uint32_t PositionMs() const { return position_ms_; }
    void SetPlaybackProgress(uint32_t position_ms, uint32_t duration_ms);
    const std::string& CurrentLine() const { return current_; }
    const std::string& PreviousLine() const { return previous_; }

private:
    bool active_ = false;
    std::string title_;
    std::string artist_;
    std::string station_;
    uint32_t duration_ms_ = 0;
    uint32_t position_ms_ = 0;
    std::string current_;
    std::string previous_;
    static std::string ClipUtf8(std::string_view text, size_t limit);
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_MUSIC_SESSION_H_
