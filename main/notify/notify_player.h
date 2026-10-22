#ifndef NOTIFY_PLAYER_H_
#define NOTIFY_PLAYER_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio_service.h"

struct NotifySubtitle {
    uint32_t start_ms = 0;
    std::string text;
};

// Playback policy shared by finite notifications and long-lived streaming
// sources (radio, music, and future media providers).
struct StreamPlaybackOptions {
    bool continuous = false;
    uint32_t reconnect_delay_ms = 1000;
    AudioPlaybackSource source = AudioPlaybackSource::Notification;
};

// Optional byte source for local media. The notification player remains
// responsible for stream decoding and output queueing; board-owned storage only
// supplies a bounded reader.
class NotifyAudioInput {
public:
    virtual ~NotifyAudioInput() = default;
    virtual bool Open() = 0;
    virtual bool Read(char* buffer, size_t capacity, size_t& bytes_read) = 0;
    virtual void Close() = 0;
};

class NotifyPlayer {
public:
    using SubtitleCallback = std::function<void(uint32_t playback_id, const std::string& text)>;
    using FinishedCallback = std::function<void(uint32_t playback_id, bool success)>;
    using InputFactory = std::function<std::unique_ptr<NotifyAudioInput>()>;

    explicit NotifyPlayer(AudioService& audio_service);
    ~NotifyPlayer();

    bool Start(std::string audio_url, std::vector<NotifySubtitle> subtitles, uint32_t playback_id,
               SubtitleCallback subtitle_callback, FinishedCallback finished_callback,
               StreamPlaybackOptions options = {}, InputFactory input_factory = {});
    void Stop();
    void OnPlaybackProgress(uint32_t playback_id, uint32_t media_position_ms);
    void OnPlaybackDrained();
    bool IsActive(uint32_t playback_id = 0) const;
    bool IsBusy() const;

private:
    AudioService& audio_service_;
    mutable std::mutex mutex_;
    std::string audio_url_;
    std::vector<NotifySubtitle> subtitles_;
    std::string displayed_text_;
    SubtitleCallback subtitle_callback_;
    FinishedCallback finished_callback_;
    StreamPlaybackOptions options_;
    InputFactory input_factory_;
    TaskHandle_t task_handle_ = nullptr;
    uint32_t playback_id_ = 0;
    uint32_t last_playback_position_ms_ = 0;
    uint32_t underrun_count_ = 0;
    size_t next_subtitle_index_ = 0;
    bool active_ = false;
    bool worker_running_ = false;
    bool cancelled_ = false;
    bool http_finished_ = false;
    bool stream_started_ = false;
    bool playback_drained_ = false;
    bool completion_reported_ = false;

    static void WorkerEntry(void* arg);
    void WorkerTask();
    bool IsCancelled(uint32_t playback_id) const;
    FinishedCallback CompleteLocked(uint32_t& playback_id);
};

#endif  // NOTIFY_PLAYER_H_
