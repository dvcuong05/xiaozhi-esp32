#ifndef AUDIO_PLAYBACK_FRAME_H_
#define AUDIO_PLAYBACK_FRAME_H_

#include <array>
#include <cstddef>
#include <cstdint>

// Internal playback metadata, never serialized into the transport wire format.
enum class AudioPlaybackSource : uint8_t { Conversation, Effect, Notification, Media };

// One bounded snapshot for optional display consumers. PCM is mono, after output
// resampling. No decoder-owned pointer crosses the audio/main-task boundary.
struct AudioPlaybackFrame {
    static constexpr size_t kSamples = 512;
    std::array<int16_t, kSamples> pcm{};
    size_t sample_count = 0;
    int sample_rate = 0;
    uint32_t playback_id = 0;
    AudioPlaybackSource source = AudioPlaybackSource::Effect;
};

#endif  // AUDIO_PLAYBACK_FRAME_H_
