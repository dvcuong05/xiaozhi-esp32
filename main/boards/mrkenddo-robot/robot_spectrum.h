#ifndef MRKENDDO_ROBOT_SPECTRUM_H_
#define MRKENDDO_ROBOT_SPECTRUM_H_

#include "audio/audio_playback_frame.h"
#include "dl_rfft.h"

#include <array>

namespace mrkenddo {

// Board-local DSP. Called only on the main task, never from audio callbacks.
// Reuses the dl_fft component already supplied by ESP-SR, as in develop_vn.
class RobotSpectrum {
public:
    static constexpr size_t kBands = 12;
    RobotSpectrum();
    ~RobotSpectrum();
    RobotSpectrum(const RobotSpectrum&) = delete;
    RobotSpectrum& operator=(const RobotSpectrum&) = delete;

    bool Analyze(const AudioPlaybackFrame& frame);
    void Reset();
    const std::array<uint8_t, kBands>& Levels() const { return levels_; }
    static uint8_t SpeechLevel(const AudioPlaybackFrame& frame);

private:
    static constexpr size_t kFftSize = AudioPlaybackFrame::kSamples;
    dl_fft_f32_t* fft_ = nullptr;
    alignas(16) std::array<float, kFftSize> buffer_{};
    std::array<float, kFftSize> window_{};
    std::array<uint8_t, kBands> levels_{};
    uint32_t playback_id_ = 0;
    int sample_rate_ = 0;
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_SPECTRUM_H_
