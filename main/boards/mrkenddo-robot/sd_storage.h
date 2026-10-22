#ifndef MRKENDDO_SD_STORAGE_H_
#define MRKENDDO_SD_STORAGE_H_

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "notify/notify_player.h"

namespace mrkenddo {

// Owns the card, mount lifetime and serialized file access. Mount/free-space
// probes run in a sleeping worker; file transfers run outside the audio/UI tasks.
class SdStorage {
public:
    enum class Code { Ok, Busy, Unavailable, Invalid, NotFound, Exists, Io, Limit };
    struct Result {
        Code code = Code::Ok;
        std::string message;
        explicit operator bool() const { return code == Code::Ok; }
    };
    struct Status {
        std::string state = "unmounted";
        std::string message;
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        bool space_valid = false;
        std::string upload_path;
        uint64_t uploaded_bytes = 0;
    };
    struct Entry {
        std::string name;
        bool directory = false;
        uint64_t size = 0;
    };
    struct Listing {
        std::vector<Entry> entries;
        bool more = false;
        size_t next_offset = 0;
    };
    struct MusicTrack {
        std::string path;
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        uint64_t size = 0;
    };
    static constexpr size_t kChunkBytes = 4096;
    // Larger HTTP chunks reduce request and worker-task overhead on the LAN.
    static constexpr size_t kUploadChunkBytes = 1024 * 1024;
    static constexpr uint64_t kMaxFileBytes = 1024ULL * 1024 * 1024;
    static constexpr size_t kPageSize = 24;

    ~SdStorage();
    void Start();
    Status GetStatus() const;
    Result Request(const std::string& action);
    Result List(const std::string& path, size_t offset, Listing& listing);
    Result SearchMusic(const std::string& query, const std::string& artist,
                       const std::string& album, const std::string& genre, size_t limit,
                       std::vector<MusicTrack>& tracks, bool& truncated);
    Result MakeDirectory(const std::string& path);
    Result Remove(const std::string& path);
    Result Upload(const std::string& path, uint64_t offset, uint64_t total, const char* chunk,
                  size_t chunk_size);
    Result AbortUpload(const std::string& path);
    Result Download(const std::string& path, const std::function<bool(const char*, size_t)>& sink);
    Result CreateAudioInput(const std::string& path, NotifyPlayer::InputFactory& factory);
    Result OpenAudioFile(const std::string& path, FILE*& file);
    bool ReadAudioFile(FILE* file, char* buffer, size_t capacity, size_t& bytes_read);
    void CloseAudioFile(FILE* file);

private:
    mutable std::mutex status_mutex_;
    std::mutex io_mutex_;
    Status status_;
    TaskHandle_t worker_ = nullptr;
    SemaphoreHandle_t stopped_ = nullptr;
    void* card_ = nullptr;
    bool bus_initialized_ = false;
    std::atomic<unsigned> playback_files_{0};
    std::string upload_path_;
    uint64_t upload_total_ = 0;
    uint64_t upload_offset_ = 0;

    static void Worker(void* context);
    void Mount();
    void Unmount();
    void RefreshSpace();
    void SetState(const char* state, const std::string& message);
    void InvalidateSpace();
    void ClearUpload();
    Result RemoveTree(const std::string& full_path, unsigned depth);
    Result IoError(const char* operation);
    Result Ready() const;
    static Result Resolve(const std::string& path, std::string& full, bool allow_root = true);
};

}  // namespace mrkenddo
#endif
