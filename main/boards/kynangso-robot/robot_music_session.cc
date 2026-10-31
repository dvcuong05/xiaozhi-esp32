#include "robot_music_session.h"

#include <algorithm>
#include <utility>

namespace kynangso {
namespace {

std::string_view Trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

// Match a complete progress marker, never arbitrary prose mentioning a tool.
std::string_view ToolName(std::string_view text) {
    if (!text.starts_with('%')) {
        return {};
    }
    text = Trim(text.substr(1));
    const auto end =
        text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
    const auto name = text.substr(0, end);
    const auto suffix = end == std::string_view::npos ? std::string_view{} : Trim(text.substr(end));
    if (suffix.empty() || suffix == "..." || suffix == "…") {
        return name;
    }
    return {};
}

}  // namespace

std::string RobotMusicSession::ClipUtf8(std::string_view text, size_t limit) {
    size_t end = std::min(text.size(), limit);
    while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
        --end;
    }
    return std::string(text.substr(0, end));
}

void RobotMusicSession::Reset() {
    active_ = false;
    title_.clear();
    artist_.clear();
    station_.clear();
    duration_ms_ = 0;
    position_ms_ = 0;
    current_.clear();
    previous_.clear();
}

void RobotMusicSession::SetMediaMetadata(std::string_view title, std::string_view artist,
                                         std::string_view station, uint32_t duration_ms) {
    Reset();
    if (title.empty() && artist.empty() && station.empty()) {
        return;
    }
    active_ = true;
    title_ = ClipUtf8(station.empty() ? title : station, 192);
    artist_ = ClipUtf8(artist, 128);
    station_ = ClipUtf8(station, 128);
    duration_ms_ = duration_ms;
}

void RobotMusicSession::SetPlaybackProgress(uint32_t position_ms, uint32_t duration_ms) {
    if (duration_ms > 0) {
        duration_ms_ = duration_ms;
    }
    position_ms_ = duration_ms_ > 0 ? std::min(position_ms, duration_ms_) : position_ms;
}

void RobotMusicSession::SetSubtitle(std::string_view text) {
    if (!active_ || text.empty()) {
        return;
    }
    previous_ = std::move(current_);
    current_ = ClipUtf8(text, 384);
}

RobotMusicSession::Update RobotMusicSession::OnAssistantText(std::string_view text) {
    text = Trim(text);
    const auto tool = ToolName(text);
    if (tool == "play_music") {
        Reset();
        active_ = true;
        return Update::Started;
    }
    if (tool == "stop_music" || tool == "pause_music") {
        const bool was_active = active_;
        Reset();
        return was_active ? Update::Stopped : Update::None;
    }
    // Search/other tool progress is not a lyric and cannot start music mode.
    if (!active_ || text.empty() || text.starts_with('%')) {
        return Update::None;
    }
    if (text.starts_with("《") && text.ends_with("》")) {
        title_ = ClipUtf8(text, 192);
    } else if (text != current_) {
        SetSubtitle(text);
    }
    return Update::Text;
}

}  // namespace kynangso
