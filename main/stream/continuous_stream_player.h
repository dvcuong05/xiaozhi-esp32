#ifndef CONTINUOUS_STREAM_PLAYER_H_
#define CONTINUOUS_STREAM_PLAYER_H_

#include <cstdint>
#include <string>

#include "audio_service.h"
#include "notify/notify_player.h"

// Device-level adapter for any continuous HTTP/Ogg stream. It intentionally
// contains no provider, station or radio-specific discovery logic.
class ContinuousStreamPlayer {
public:
    using FinishedCallback = NotifyPlayer::FinishedCallback;

    explicit ContinuousStreamPlayer(AudioService& audio_service);

    bool Start(std::string audio_url, uint32_t playback_id, FinishedCallback finished_callback);
    void Stop();
    void OnPlaybackProgress(uint32_t playback_id, uint32_t media_position_ms);
    void OnPlaybackDrained();
    bool IsBusy() const;

private:
    NotifyPlayer stream_player_;
};

#endif  // CONTINUOUS_STREAM_PLAYER_H_
