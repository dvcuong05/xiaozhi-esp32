#include "robot_spectrum.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>

namespace mrkenddo {

RobotSpectrum::RobotSpectrum() {
    fft_ = dl_rfft_f32_init(kFftSize, MALLOC_CAP_8BIT);
    if (fft_ == nullptr) {
        ESP_LOGE("RobotSpectrum", "Cannot initialize FFT; media keeps robot face");
    }
    for (size_t i = 0; i < kFftSize; ++i) {
        window_[i] = 0.5f - 0.5f * std::cos(6.28318530718f * i / (kFftSize - 1));
    }
}

RobotSpectrum::~RobotSpectrum() {
    if (fft_ != nullptr) {
        dl_rfft_f32_deinit(fft_);
    }
}

void RobotSpectrum::Reset() {
    levels_.fill(0);
    playback_id_ = 0;
    sample_rate_ = 0;
}

bool RobotSpectrum::Analyze(const AudioPlaybackFrame& frame) {
    if (fft_ == nullptr || frame.sample_rate <= 0 || frame.sample_count < 2 ||
        frame.sample_count > kFftSize) {
        return false;
    }
    if (playback_id_ != frame.playback_id || sample_rate_ != frame.sample_rate) {
        Reset();
        playback_id_ = frame.playback_id;
        sample_rate_ = frame.sample_rate;
    }

    // Remove DC before the Hann window so offsets do not masquerade as bass.
    float mean = 0;
    for (size_t i = 0; i < frame.sample_count; ++i) {
        mean += frame.pcm[i];
    }
    mean /= frame.sample_count;
    float window_sum = 0;
    buffer_.fill(0);
    for (size_t i = 0; i < frame.sample_count; ++i) {
        buffer_[i] = (frame.pcm[i] - mean) / 32768.0f * window_[i];
        window_sum += window_[i];
    }
    if (dl_rfft_f32_run(fft_, buffer_.data()) != ESP_OK) {
        return false;
    }

    // Fixed Hz edges (bass -> treble), limited by the actual output Nyquist.
    // Packed RFFT: [DC, Nyquist, Re(1), Im(1), ...]. Never include DC.
    constexpr float kEdges[kBands + 1] = {
        40, 80, 140, 240, 400, 650, 1000, 1600, 2500, 4000, 6300, 10000, 16000,
    };
    const float scale = 4.0f / std::max(1.0f, window_sum * window_sum);
    for (size_t band = 0; band < kBands; ++band) {
        const size_t begin =
            std::max<size_t>(1, std::ceil(kEdges[band] * kFftSize / frame.sample_rate));
        const size_t end = std::min<size_t>(
            kFftSize / 2, std::ceil(kEdges[band + 1] * kFftSize / frame.sample_rate));
        float peak = 0;
        for (size_t bin = begin; bin < end; ++bin) {
            const float re = buffer_[2 * bin];
            const float im = buffer_[2 * bin + 1];
            peak = std::max(peak, (re * re + im * im) * scale);
        }
        // Fixed -60..0 dBFS scale, no auto-gain that amplifies silence.
        const float db = 10.0f * std::log10(std::max(peak, 1.0e-6f));
        const int target = std::clamp(static_cast<int>((db + 60) * 255 / 60), 0, 255);
        const int previous = levels_[band];
        levels_[band] = target >= previous ? target : std::max(target, previous - 22);
    }
    return true;
}

uint8_t RobotSpectrum::SpeechLevel(const AudioPlaybackFrame& frame) {
    const size_t count = std::min(frame.sample_count, frame.pcm.size());
    if (count == 0) {
        return 0;
    }
    double power = 0;
    for (size_t i = 0; i < count; ++i) {
        const double sample = frame.pcm[i] / 32768.0;
        power += sample * sample;
    }
    const double rms = std::sqrt(power / count);
    return static_cast<uint8_t>(std::clamp((rms - 0.004) * 1800.0, 0.0, 255.0));
}

}  // namespace mrkenddo
