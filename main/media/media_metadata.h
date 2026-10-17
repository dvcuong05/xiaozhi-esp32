#ifndef MEDIA_METADATA_H_
#define MEDIA_METADATA_H_

#include <cstdint>
#include <string>

// Provider-neutral description of the currently requested media item.
struct MediaPlaybackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string station;
    uint32_t duration_ms = 0;
};

#endif  // MEDIA_METADATA_H_
