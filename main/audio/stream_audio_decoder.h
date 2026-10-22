#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class AudioStreamDecoderBackend;

// Selects one decoder from the stream signature, independently of SD/HTTP URLs.
// PCM delivered to the sink is mono S16 at the device's output sample rate.
class AudioStreamDecoder {
public:
    using OpusSink = std::function<bool(const uint8_t*, size_t, int, int, uint32_t)>;
    using PcmSink = std::function<bool(std::vector<int16_t>&&, uint32_t)>;

    AudioStreamDecoder(int output_sample_rate, OpusSink opus_sink, PcmSink pcm_sink);
    ~AudioStreamDecoder();
    bool Process(const uint8_t* data, size_t size);
    bool Finish();

private:
    int output_sample_rate_;
    OpusSink opus_sink_;
    PcmSink pcm_sink_;
    std::array<uint8_t, 4> probe_{};
    size_t probe_size_ = 0;
    bool failed_ = false;
    std::unique_ptr<AudioStreamDecoderBackend> decoder_;
};
