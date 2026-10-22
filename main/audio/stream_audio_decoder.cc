#include "stream_audio_decoder.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <esp_log.h>
#include "sdkconfig.h"

#include "ogg_demuxer.h"

#if CONFIG_ENABLE_MP3_PLAYBACK
#include <mutex>
#include "esp_ae_rate_cvt.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#endif

namespace {
constexpr const char* kTag = "StreamDecoder";
}

class AudioStreamDecoderBackend {
public:
    virtual ~AudioStreamDecoderBackend() = default;
    virtual bool Process(const uint8_t* data, size_t size) = 0;
    virtual bool Finish() = 0;
};

namespace {
class OggOpusDecoder final : public AudioStreamDecoderBackend {
public:
    explicit OggOpusDecoder(AudioStreamDecoder::OpusSink sink) {
        demuxer_.OnPacket([this, sink = std::move(sink)](const uint8_t* data, int rate,
                                                         int duration, size_t size) {
            if (failed_)
                return;
            failed_ = !sink(data, size, rate, duration, position_ms_);
            position_ms_ += duration;
        });
    }
    bool Process(const uint8_t* data, size_t size) override {
        demuxer_.Process(data, size);
        return !failed_ && !demuxer_.HasError();
    }
    bool Finish() override { return !failed_ && demuxer_.Finish(); }

private:
    OggDemuxer demuxer_;
    uint32_t position_ms_ = 0;
    bool failed_ = false;
};

#if CONFIG_ENABLE_MP3_PLAYBACK
class Mp3Decoder final : public AudioStreamDecoderBackend {
public:
    Mp3Decoder(int output_rate, AudioStreamDecoder::PcmSink sink)
        : output_rate_(output_rate), sink_(std::move(sink)) {}
    ~Mp3Decoder() override {
        if (decoder_)
            esp_audio_simple_dec_close(decoder_);
        if (resampler_)
            esp_ae_rate_cvt_close(resampler_);
    }

    bool Process(const uint8_t* data, size_t size) override {
        if (!decoder_ && !Open())
            return false;
        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = const_cast<uint8_t*>(data);
        raw.len = size;
        while (raw.len != 0) {
            raw.consumed = 0;
            esp_audio_simple_dec_out_t output = {};
            output.buffer = reinterpret_cast<uint8_t*>(decoded_.data());
            output.len = decoded_.size() * sizeof(int16_t);
            auto result = esp_audio_simple_dec_process(decoder_, &raw, &output);
            if (result != ESP_AUDIO_ERR_OK || raw.consumed > raw.len) {
                ESP_LOGE(kTag, "MP3 decode failed: %d", result);
                return false;
            }
            if (output.decoded_size != 0 && !Output(output.decoded_size))
                return false;
            if (raw.consumed == 0 && output.decoded_size == 0) {
                ESP_LOGE(kTag, "MP3 decoder made no progress");
                return false;
            }
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }
        return true;
    }

    bool Finish() override { return decoded_frames_ != 0; }

private:
    bool Open() {
        static std::once_flag registered;
        static esp_audio_err_t registration_result = ESP_AUDIO_ERR_FAIL;
        std::call_once(registered, []() { registration_result = esp_mp3_dec_register(); });
        if (registration_result != ESP_AUDIO_ERR_OK)
            return false;
        esp_audio_simple_dec_cfg_t config = {};
        config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        config.use_frame_dec = false;
        auto result = esp_audio_simple_dec_open(&config, &decoder_);
        if (result != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(kTag, "Cannot open MP3 decoder: %d", result);
            return false;
        }
        return true;
    }

    bool Output(size_t bytes) {
        esp_audio_simple_dec_info_t info = {};
        if (esp_audio_simple_dec_get_info(decoder_, &info) != ESP_AUDIO_ERR_OK ||
            info.bits_per_sample != 16 || info.channel < 1 || info.channel > 2 ||
            info.sample_rate < 8000 || info.sample_rate > 48000 ||
            bytes > decoded_.size() * sizeof(int16_t) || bytes % (info.channel * 2) != 0)
            return false;
        if (input_rate_ != info.sample_rate) {
            if (resampler_) {
                esp_ae_rate_cvt_close(resampler_);
                resampler_ = nullptr;
            }
            time_remainder_ = 0;
            if (info.sample_rate != static_cast<uint32_t>(output_rate_)) {
                esp_ae_rate_cvt_cfg_t config = {};
                config.src_rate = info.sample_rate;
                config.dest_rate = output_rate_;
                config.channel = 1;
                config.bits_per_sample = 16;
                config.complexity = 2;
                config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                if (esp_ae_rate_cvt_open(&config, &resampler_) != ESP_AE_ERR_OK)
                    return false;
            }
            input_rate_ = info.sample_rate;
            ESP_LOGI(kTag, "MP3: %lu Hz, %u channels -> %d Hz mono",
                     static_cast<unsigned long>(input_rate_), info.channel, output_rate_);
        }
        const uint32_t samples = bytes / (info.channel * sizeof(int16_t));
        if (info.channel == 2) {
            for (size_t i = 0; i < samples; ++i)
                decoded_[i] = (static_cast<int32_t>(decoded_[i * 2]) + decoded_[i * 2 + 1]) / 2;
        }
        std::vector<int16_t> pcm;
        if (resampler_) {
            uint32_t count = 0;
            if (esp_ae_rate_cvt_get_max_out_sample_num(resampler_, samples, &count) !=
                    ESP_AE_ERR_OK ||
                count > 8192)
                return false;
            pcm.resize(count);
            if (esp_ae_rate_cvt_process(resampler_, decoded_.data(), samples, pcm.data(), &count) !=
                ESP_AE_ERR_OK)
                return false;
            pcm.resize(count);
        } else {
            pcm.assign(decoded_.begin(), decoded_.begin() + samples);
        }
        if (!pcm.empty() && !sink_(std::move(pcm), static_cast<uint32_t>(position_us_ / 1000)))
            return false;
        const uint64_t duration = static_cast<uint64_t>(samples) * 1000000 + time_remainder_;
        position_us_ += duration / input_rate_;
        time_remainder_ = duration % input_rate_;
        ++decoded_frames_;
        return true;
    }

    int output_rate_;
    AudioStreamDecoder::PcmSink sink_;
    esp_audio_simple_dec_handle_t decoder_ = nullptr;
    esp_ae_rate_cvt_handle_t resampler_ = nullptr;
    std::array<int16_t, 2304> decoded_{};
    uint32_t input_rate_ = 0;
    uint64_t position_us_ = 0;
    uint64_t time_remainder_ = 0;
    size_t decoded_frames_ = 0;
};
#endif
}  // namespace

AudioStreamDecoder::AudioStreamDecoder(int output_sample_rate, OpusSink opus_sink, PcmSink pcm_sink)
    : output_sample_rate_(output_sample_rate),
      opus_sink_(std::move(opus_sink)),
      pcm_sink_(std::move(pcm_sink)) {}

AudioStreamDecoder::~AudioStreamDecoder() = default;

bool AudioStreamDecoder::Process(const uint8_t* data, size_t size) {
    if (failed_ || (!data && size))
        return false;
    if (!decoder_) {
        const size_t copied = std::min(size, probe_.size() - probe_size_);
        if (copied != 0) {
            std::memcpy(probe_.data() + probe_size_, data, copied);
            probe_size_ += copied;
            data += copied;
            size -= copied;
        }
        if (probe_size_ < probe_.size())
            return true;
        if (std::memcmp(probe_.data(), "OggS", 4) == 0) {
            decoder_ = std::make_unique<OggOpusDecoder>(std::move(opus_sink_));
            ESP_LOGI(kTag, "Selected Ogg/Opus stream");
#if CONFIG_ENABLE_MP3_PLAYBACK
        } else if (std::memcmp(probe_.data(), "ID3", 3) == 0 ||
                   (probe_[0] == 0xFF && (probe_[1] & 0xE6) == 0xE2 && (probe_[1] & 0x18) != 0x08 &&
                    (probe_[2] & 0xF0) != 0xF0 && (probe_[2] & 0x0C) != 0x0C)) {
            decoder_ = std::make_unique<Mp3Decoder>(output_sample_rate_, std::move(pcm_sink_));
            ESP_LOGI(kTag, "Selected MP3 stream");
#endif
        } else {
            ESP_LOGE(kTag, "Unsupported stream signature (expected Ogg/Opus or enabled MP3)");
            failed_ = true;
            return false;
        }
        if (!decoder_->Process(probe_.data(), probe_.size())) {
            failed_ = true;
            return false;
        }
    }
    if (size != 0 && !decoder_->Process(data, size))
        failed_ = true;
    return !failed_;
}

bool AudioStreamDecoder::Finish() { return !failed_ && decoder_ && decoder_->Finish(); }
