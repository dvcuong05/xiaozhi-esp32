#include "continuous_stream_player.h"

#include <utility>

ContinuousStreamPlayer::ContinuousStreamPlayer(AudioService& audio_service)
    : stream_player_(audio_service) {}

bool ContinuousStreamPlayer::Start(std::string audio_url, uint32_t playback_id,
                                   FinishedCallback finished_callback) {
    StreamPlaybackOptions options;
    options.continuous = true;
    return stream_player_.Start(std::move(audio_url), {}, playback_id, nullptr,
                                std::move(finished_callback), options);
}

void ContinuousStreamPlayer::Stop() { stream_player_.Stop(); }

void ContinuousStreamPlayer::OnPlaybackProgress(uint32_t playback_id,
                                                uint32_t media_position_ms) {
    stream_player_.OnPlaybackProgress(playback_id, media_position_ms);
}

void ContinuousStreamPlayer::OnPlaybackDrained() { stream_player_.OnPlaybackDrained(); }

bool ContinuousStreamPlayer::IsBusy() const { return stream_player_.IsBusy(); }
