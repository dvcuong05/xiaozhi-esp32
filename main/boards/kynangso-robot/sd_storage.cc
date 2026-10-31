#include "sd_storage.h"

#include "config.h"

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_vfs_fat.h>
#include <cJSON.h>
#include <sdmmc_cmd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

namespace kynangso {
namespace {
constexpr char kMount[] = "/sdcard";
constexpr char kTemporarySuffix[] = ".robot-upload";
constexpr char kMusicMetadataPath[] = "/sdcard/robot/music/.metadata.json";
constexpr char kMusicMetadataTemporaryPath[] = "/sdcard/robot/music/.metadata.json.tmp";
constexpr char kMusicMetadataBackupPath[] = "/sdcard/robot/music/.metadata.json.bak";
constexpr size_t kMusicMetadataLineBytes = 1024;
constexpr uint32_t kWorkerStackBytes = 12 * 1024;
constexpr uint32_t kMountCommand = 1;
constexpr uint32_t kUnmountCommand = 2;
constexpr uint32_t kRefreshCommand = 3;
constexpr uint32_t kStopCommand = 4;
constexpr uint32_t kMusicCommand = 5;
constexpr unsigned kMaxDeleteDepth = 16;
constexpr char kTag[] = "RobotSD";
using Code = SdStorage::Code;
using Result = SdStorage::Result;

class SdAudioInput final : public NotifyAudioInput {
public:
    SdAudioInput(SdStorage& storage, std::string path)
        : storage_(storage), path_(std::move(path)) {}
    ~SdAudioInput() override { Close(); }

    bool Open() override { return static_cast<bool>(storage_.OpenAudioFile(path_, file_)); }
    bool Read(char* buffer, size_t capacity, size_t& bytes_read) override {
        return storage_.ReadAudioFile(file_, buffer, capacity, bytes_read);
    }
    void Close() override {
        if (file_ != nullptr) {
            storage_.CloseAudioFile(file_);
            file_ = nullptr;
        }
    }

private:
    SdStorage& storage_;
    std::string path_;
    FILE* file_ = nullptr;
};

bool ValidUtf8(const std::string& text) {
    uint32_t value = 0, minimum = 0;
    int left = 0;
    for (unsigned char c : text) {
        if (left) {
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 0x3f);
            if (--left == 0 &&
                (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)))
                return false;
        } else if (c < 0x80) {
            continue;
        } else if (c >= 0xc2 && c <= 0xdf) {
            left = 1;
            value = c & 0x1f;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            left = 2;
            value = c & 0x0f;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            left = 3;
            value = c & 7;
            minimum = 0x10000;
        } else
            return false;
    }
    return left == 0;
}

void AppendUtf8(std::string& output, uint32_t value) {
    if (value < 0x80) {
        output.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value < 0x10000 && (value < 0xd800 || value > 0xdfff)) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value >= 0x10000 && value <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

std::string DecodeId3Text(const uint8_t* data, size_t size) {
    if (!data || size < 2)
        return {};
    const uint8_t encoding = data[0];
    std::string result;
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 1; i < size && data[i] != 0; ++i) {
            if (result.size() >= 128)
                break;
            if (encoding == 3) {
                result.push_back(static_cast<char>(data[i]));
            } else {
                AppendUtf8(result, data[i]);
            }
        }
        if (ValidUtf8(result))
            return result;
        return {};
    }
    if (encoding != 1 && encoding != 2)
        return {};
    size_t offset = 1;
    bool little_endian = encoding == 1;
    if (encoding == 1 && size >= 3 && data[1] == 0xff && data[2] == 0xfe) {
        offset = 3;
        little_endian = true;
    } else if (encoding == 1 && size >= 3 && data[1] == 0xfe && data[2] == 0xff) {
        offset = 3;
        little_endian = false;
    }
    while (offset + 1 < size) {
        uint32_t code = little_endian
                            ? static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8))
                            : static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
        offset += 2;
        if (code == 0)
            break;
        if (code >= 0xd800 && code <= 0xdbff && offset + 1 < size) {
            uint16_t low = little_endian
                               ? static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8))
                               : static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                offset += 2;
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        AppendUtf8(result, code);
        if (result.size() >= 128)
            break;
    }
    return result;
}

uint32_t ReadSynchsafe(const uint8_t* value) {
    return (static_cast<uint32_t>(value[0] & 0x7f) << 21) |
           (static_cast<uint32_t>(value[1] & 0x7f) << 14) |
           (static_cast<uint32_t>(value[2] & 0x7f) << 7) | (value[3] & 0x7f);
}

std::string FoldText(const std::string& value) {
    std::string folded;
    folded.reserve(value.size());
    for (size_t i = 0; i < value.size();) {
        const auto first = static_cast<uint8_t>(value[i]);
        uint32_t code = first;
        size_t length = 1;
        if ((first & 0xe0) == 0xc0 && i + 1 < value.size()) {
            code = ((first & 0x1f) << 6) | (static_cast<uint8_t>(value[i + 1]) & 0x3f);
            length = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < value.size()) {
            code = ((first & 0x0f) << 12) | ((static_cast<uint8_t>(value[i + 1]) & 0x3f) << 6) |
                   (static_cast<uint8_t>(value[i + 2]) & 0x3f);
            length = 3;
        } else if ((first & 0xf8) == 0xf0 && i + 3 < value.size()) {
            code = ((first & 7) << 18) | ((static_cast<uint8_t>(value[i + 1]) & 0x3f) << 12) |
                   ((static_cast<uint8_t>(value[i + 2]) & 0x3f) << 6) |
                   (static_cast<uint8_t>(value[i + 3]) & 0x3f);
            length = 4;
        }
        if (code >= 'A' && code <= 'Z')
            code += 'a' - 'A';
        else if ((code >= 0x00c0 && code <= 0x00d6) || (code >= 0x00d8 && code <= 0x00de))
            code += 0x20;
        else if ((code >= 0x0100 && code <= 0x012f && code % 2 == 0) ||
                 (code >= 0x0132 && code <= 0x0137 && code % 2 == 0) ||
                 (code >= 0x0139 && code <= 0x0148 && code % 2 == 1) ||
                 (code >= 0x014a && code <= 0x0177 && code % 2 == 0) ||
                 (code >= 0x0179 && code <= 0x017e && code % 2 == 1) ||
                 (code >= 0x1ea0 && code <= 0x1ef8 && code % 2 == 0))
            ++code;
        else if (code == 0x0178)
            code = 0x00ff;
        AppendUtf8(folded, code);
        i += length;
    }
    return folded;
}

bool HasAudioExtension(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    auto extension = FoldText(path.substr(dot));
    return extension == ".mp3" || extension == ".ogg" || extension == ".opus";
}

std::string TrimText(std::string text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

// Music keys are independent of display text and photo matching. Vietnamese
// precomposed and decomposed accents both match an unaccented voice query.
std::string MusicKey(const std::string& text) {
    const auto folded = FoldText(text);
    const std::string_view groups[] = {
        "àáảãạăằắẳẵặâầấẩẫậ", "èéẻẽẹêềếểễệ", "ìíỉĩị", "òóỏõọôồốổỗộơờớởỡợƠ",
        "ùúủũụưừứửữựƯ",      "ỳýỷỹỵ",       "đ"};
    constexpr char replacements[] = "aeiouyd";
    std::string key;
    for (size_t i = 0; i < folded.size();) {
        const auto c = static_cast<uint8_t>(folded[i]);
        const size_t length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        const auto token = folded.substr(i, length);
        i += length;
        if (length == 2 && c == 0xcc)
            continue;  // U+0300..033F includes all Vietnamese combining marks.
        bool replaced = false;
        for (size_t group = 0; group < 7; ++group) {
            if (length > 1 && groups[group].find(token) != std::string_view::npos) {
                key.push_back(replacements[group]);
                replaced = true;
                break;
            }
        }
        if (replaced)
            continue;
        if (length == 1 && !std::isalnum(c)) {
            if (!key.empty() && key.back() != ' ')
                key.push_back(' ');
        } else if (length == 1) {
            key.push_back(static_cast<char>(std::tolower(c)));
        } else {
            key += token;
        }
    }
    return TrimText(std::move(key));
}

void CopyMusicText(char* output, size_t capacity, const std::string& text) {
    size_t size = std::min(text.size(), capacity - 1);
    while (size > 0 && !ValidUtf8(text.substr(0, size)))
        --size;
    std::memcpy(output, text.data(), size);
    output[size] = 0;
}

bool ValidMusicMetadataValue(const std::string& value) {
    if (value.size() > 96 || !ValidUtf8(value))
        return false;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
}

bool ValidMusicMetadataName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." ||
        name.size() + sizeof(SdStorage::kMusicRoot) > 192 || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos || !HasAudioExtension(name) || !ValidUtf8(name))
        return false;
    return std::all_of(name.begin(), name.end(),
                       [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
}

char* TrimMusicMetadataLine(char* line) {
    while (*line && std::isspace(static_cast<unsigned char>(*line)))
        ++line;
    size_t length = std::strlen(line);
    while (length && std::isspace(static_cast<unsigned char>(line[length - 1])))
        line[--length] = 0;
    if (length && line[length - 1] == ',') {
        line[--length] = 0;
        while (length && std::isspace(static_cast<unsigned char>(line[length - 1])))
            line[--length] = 0;
    }
    return line;
}

bool ParseMusicMetadataLine(char* line, std::string& path, std::string& artist, std::string& album,
                            std::string& genre, bool& has_path, bool& has_artist, bool& has_album,
                            bool& has_genre, bool& has_version, bool& is_object) {
    path.clear();
    artist.clear();
    album.clear();
    genre.clear();
    has_path = has_artist = has_album = has_genre = has_version = is_object = false;
    char* json = TrimMusicMetadataLine(line);
    if (*json == 0 || *json == '[' || *json == ']')
        return true;
    if (*json != '{')
        return false;

    cJSON* root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    is_object = true;
    const auto* version_item = cJSON_GetObjectItemCaseSensitive(root, "version");
    has_version = version_item != nullptr;
    const auto* path_item = cJSON_GetObjectItemCaseSensitive(root, "path");
    const auto* name_item = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (path_item && name_item) {
        cJSON_Delete(root);
        return false;
    }
    if (path_item || name_item) {
        const auto* identity_item = name_item ? name_item : path_item;
        if (!cJSON_IsString(identity_item) || identity_item->valuestring == nullptr) {
            cJSON_Delete(root);
            return false;
        }
        const std::string stored_identity = identity_item->valuestring;
        std::string name;
        if (name_item) {
            name = stored_identity;
        } else if (stored_identity.compare(0, sizeof(SdStorage::kMusicRoot) - 1,
                                           SdStorage::kMusicRoot) == 0) {
            if (stored_identity.size() <= sizeof(SdStorage::kMusicRoot) ||
                stored_identity[sizeof(SdStorage::kMusicRoot) - 1] != '/') {
                cJSON_Delete(root);
                return false;
            }
            name = stored_identity.substr(sizeof(SdStorage::kMusicRoot));
        } else {
            name = stored_identity;
        }
        if (!ValidMusicMetadataName(name)) {
            cJSON_Delete(root);
            return false;
        }
        path = std::string(SdStorage::kMusicRoot) + "/" + name;
        has_path = true;
    }

    auto read_string = [root](const char* key, std::string& value, bool& present) {
        const auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (!item)
            return true;
        if (!cJSON_IsString(item) || item->valuestring == nullptr)
            return false;
        value = item->valuestring;
        present = true;
        return ValidMusicMetadataValue(value);
    };
    const bool valid = read_string("artist", artist, has_artist) &&
                       read_string("album", album, has_album) &&
                       read_string("genre", genre, has_genre);
    cJSON_Delete(root);
    if (!valid)
        return false;
    return true;
}

bool WriteMusicMetadataObject(FILE* file, const char* json, bool& first) {
    if (!first && std::fputs(",\n", file) < 0)
        return false;
    if (std::fputs(json, file) < 0)
        return false;
    first = false;
    return true;
}

bool WriteMusicMetadataRecord(FILE* file, const std::string& path, const std::string& artist,
                              const std::string& album, const std::string& genre, bool& first) {
    cJSON* record = cJSON_CreateObject();
    if (!record)
        return false;
    const auto name = path.substr(path.find_last_of('/') + 1);
    const bool added = cJSON_AddStringToObject(record, "name", name.c_str()) &&
                       cJSON_AddStringToObject(record, "artist", artist.c_str()) &&
                       cJSON_AddStringToObject(record, "album", album.c_str()) &&
                       cJSON_AddStringToObject(record, "genre", genre.c_str());
    char* json = added ? cJSON_PrintUnformatted(record) : nullptr;
    cJSON_Delete(record);
    if (!json)
        return false;
    const bool written = WriteMusicMetadataObject(file, json, first);
    cJSON_free(json);
    return written;
}

bool CompactMusicMetadataRecord(const char* source, std::string& compact) {
    cJSON* record = cJSON_Parse(source);
    if (!record || !cJSON_IsObject(record)) {
        cJSON_Delete(record);
        return false;
    }
    const auto* path_item = cJSON_GetObjectItemCaseSensitive(record, "path");
    if (path_item) {
        const std::string path = path_item->valuestring;
        const auto name = path.substr(path.find_last_of('/') + 1);
        cJSON_DeleteItemFromObjectCaseSensitive(record, "path");
        if (!cJSON_AddStringToObject(record, "name", name.c_str())) {
            cJSON_Delete(record);
            return false;
        }
    }
    char* json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json)
        return false;
    compact = json;
    cJSON_free(json);
    return true;
}

std::string GenreKey(const std::string& text) {
    // ID3v2 may store an ID3v1 numeric genre such as (17) instead of Rock.
    static constexpr const char* names[] = {"Blues",
                                            "Classic Rock",
                                            "Country",
                                            "Dance",
                                            "Disco",
                                            "Funk",
                                            "Grunge",
                                            "Hip-Hop",
                                            "Jazz",
                                            "Metal",
                                            "New Age",
                                            "Oldies",
                                            "Other",
                                            "Pop",
                                            "R&B",
                                            "Rap",
                                            "Reggae",
                                            "Rock",
                                            "Techno",
                                            "Industrial",
                                            "Alternative",
                                            "Ska",
                                            "Death Metal",
                                            "Pranks",
                                            "Soundtrack",
                                            "Euro-Techno",
                                            "Ambient",
                                            "Trip-Hop",
                                            "Vocal",
                                            "Jazz+Funk",
                                            "Fusion",
                                            "Trance",
                                            "Classical",
                                            "Instrumental",
                                            "Acid",
                                            "House",
                                            "Game",
                                            "Sound Clip",
                                            "Gospel",
                                            "Noise",
                                            "Alternative Rock",
                                            "Bass",
                                            "Soul",
                                            "Punk",
                                            "Space",
                                            "Meditative",
                                            "Instrumental Pop",
                                            "Instrumental Rock",
                                            "Ethnic",
                                            "Gothic",
                                            "Darkwave",
                                            "Techno-Industrial",
                                            "Electronic",
                                            "Pop-Folk",
                                            "Eurodance",
                                            "Dream",
                                            "Southern Rock",
                                            "Comedy",
                                            "Cult",
                                            "Gangsta",
                                            "Top 40",
                                            "Christian Rap",
                                            "Pop/Funk",
                                            "Jungle",
                                            "Native American",
                                            "Cabaret",
                                            "New Wave",
                                            "Psychedelic",
                                            "Rave",
                                            "Showtunes",
                                            "Trailer",
                                            "Lo-Fi",
                                            "Tribal",
                                            "Acid Punk",
                                            "Acid Jazz",
                                            "Polka",
                                            "Retro",
                                            "Musical",
                                            "Rock & Roll",
                                            "Hard Rock"};
    auto value = TrimText(text);
    if (value.size() > 2 && value.front() == '(' && value.back() == ')')
        value = value.substr(1, value.size() - 2);
    if (!value.empty() && value.size() <= 3 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        const auto number = std::strtoul(value.c_str(), nullptr, 10);
        if (number < sizeof(names) / sizeof(names[0]))
            value = names[number];
    }
    auto key = MusicKey(value);
    if (key == "nhac tru tinh" || key == "tru tinh")
        return "tru tinh";
    if (key == "nhac tre" || key == "nhac pop")
        return "pop";
    if (key == "nhac rock")
        return "rock";
    return key;
}

void ReadId3Metadata(const std::string& full_path, SdStorage::MusicTrack& track, uint8_t* tag,
                     size_t capacity) {
    track.title = track.path.substr(track.path.find_last_of('/') + 1);
    auto dot = track.title.find_last_of('.');
    if (dot != std::string::npos)
        track.title.resize(dot);
    // Optional filename convention: Artist - Title - Album - Genre.
    std::vector<std::string> parts;
    size_t start = 0, split = 0;
    while (parts.size() < 3 && (split = track.title.find(" - ", start)) != std::string::npos) {
        parts.push_back(TrimText(track.title.substr(start, split - start)));
        start = split + 3;
    }
    parts.push_back(TrimText(track.title.substr(start)));
    if (parts.size() >= 2) {
        track.artist = parts[0];
        track.title = parts[1];
        if (parts.size() >= 3)
            track.album = parts[2];
        if (parts.size() >= 4)
            track.genre = parts[3];
    }
    if (FoldText(track.path.substr(track.path.find_last_of('.'))) != ".mp3" || !tag ||
        capacity == 0)
        return;

    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full_path.c_str(), "rb"), fclose);
    if (!file)
        return;
    std::array<uint8_t, 10> header = {};
    if (fread(header.data(), 1, header.size(), file.get()) != header.size() ||
        std::memcmp(header.data(), "ID3", 3) != 0 || header[3] < 3 || header[3] > 4)
        return;
    // Skip artwork with seeks, rather than reading the first 16 KiB of a tag.
    // Text frames after APIC remain discoverable. Unsupported tag transforms
    // safely fall back to the filename rather than returning corrupted text.
    if (header[5] & 0xc0)
        return;  // Unsynchronisation / extended headers.
    const uint32_t tag_size = ReadSynchsafe(header.data() + 6);
    if (tag_size > 4 * 1024 * 1024)
        return;
    size_t offset = 0;
    std::array<uint8_t, 10> frame_header;
    for (unsigned frames = 0; frames < 256 && offset + 10 <= tag_size; ++frames) {
        if (fseek(file.get(), 10 + offset, SEEK_SET) != 0 ||
            fread(frame_header.data(), 1, 10, file.get()) != 10)
            break;
        const uint8_t* frame = frame_header.data();
        if (frame[0] == 0)
            break;
        uint32_t frame_size = header[3] == 4
                                  ? ReadSynchsafe(frame + 4)
                                  : (static_cast<uint32_t>(frame[4]) << 24) |
                                        (static_cast<uint32_t>(frame[5]) << 16) |
                                        (static_cast<uint32_t>(frame[6]) << 8) | frame[7];
        if (frame_size == 0 || frame_size > tag_size - offset - 10)
            break;
        std::string* destination = nullptr;
        const bool duration_frame = std::memcmp(frame, "TLEN", 4) == 0;
        if (std::memcmp(frame, "TIT2", 4) == 0)
            destination = &track.title;
        else if (std::memcmp(frame, "TPE1", 4) == 0)
            destination = &track.artist;
        else if (std::memcmp(frame, "TALB", 4) == 0)
            destination = &track.album;
        else if (std::memcmp(frame, "TCON", 4) == 0)
            destination = &track.genre;
        if ((destination || duration_frame) && frame[9] == 0 && frame_size <= capacity &&
            fread(tag, 1, frame_size, file.get()) == frame_size) {
            auto value = TrimText(DecodeId3Text(tag, frame_size));
            if (duration_frame && !value.empty() && value.size() <= 8 &&
                std::all_of(value.begin(), value.end(),
                            [](unsigned char c) { return std::isdigit(c); })) {
                const auto duration = std::strtoul(value.c_str(), nullptr, 10);
                if (duration <= 24UL * 60 * 60 * 1000)
                    track.duration_ms = static_cast<uint32_t>(duration);
            } else if (destination && !value.empty()) {
                *destination = std::move(value);
            }
        }
        offset += 10 + frame_size;
    }
}

struct Mp3FrameInfo {
    uint8_t version_id = 0;
    uint8_t layer_id = 0;
    bool mono = false;
    bool has_crc = false;
    uint32_t bitrate_kbps = 0;
    uint32_t sample_rate = 0;
    uint16_t samples_per_frame = 0;
    uint32_t frame_size = 0;
};

bool ParseMp3FrameHeader(const uint8_t* bytes, Mp3FrameInfo& frame) {
    const uint32_t header = (static_cast<uint32_t>(bytes[0]) << 24) |
                            (static_cast<uint32_t>(bytes[1]) << 16) |
                            (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
    if ((header & 0xffe00000) != 0xffe00000)
        return false;

    const uint8_t version = (header >> 19) & 0x03;
    const uint8_t layer = (header >> 17) & 0x03;
    const uint8_t bitrate_index = (header >> 12) & 0x0f;
    const uint8_t sample_index = (header >> 10) & 0x03;
    if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 ||
        sample_index == 3)
        return false;

    static constexpr uint16_t kMpeg1Bitrates[3][14] = {
        {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
        {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
        {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    };
    static constexpr uint16_t kMpeg2Bitrates[3][14] = {
        {32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
        {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
    };
    static constexpr uint32_t kBaseSampleRates[] = {44100, 48000, 32000};

    const size_t layer_index = 3 - layer;
    frame.version_id = version;
    frame.layer_id = layer;
    frame.mono = ((header >> 6) & 0x03) == 3;
    frame.has_crc = ((header >> 16) & 0x01) == 0;
    frame.bitrate_kbps =
        (version == 3 ? kMpeg1Bitrates : kMpeg2Bitrates)[layer_index][bitrate_index - 1];
    frame.sample_rate = kBaseSampleRates[sample_index] / (version == 3 ? 1 : version == 2 ? 2 : 4);
    frame.samples_per_frame = layer == 3 ? 384 : layer == 2 ? 1152 : version == 3 ? 1152 : 576;

    const uint32_t bitrate_bps = frame.bitrate_kbps * 1000;
    const uint32_t padding = (header >> 9) & 0x01;
    uint64_t frame_size = 0;
    if (layer == 3) {
        frame_size = (12ULL * bitrate_bps / frame.sample_rate + padding) * 4;
    } else {
        const uint32_t coefficient = layer == 1 && version != 3 ? 72 : 144;
        frame_size = coefficient * static_cast<uint64_t>(bitrate_bps) / frame.sample_rate + padding;
    }
    if (frame_size < 4 || frame_size > 4096)
        return false;
    frame.frame_size = static_cast<uint32_t>(frame_size);
    return true;
}

bool SameMp3Stream(const Mp3FrameInfo& left, const Mp3FrameInfo& right) {
    return left.version_id == right.version_id && left.layer_id == right.layer_id &&
           left.sample_rate == right.sample_rate;
}

bool ReadMp3FrameAt(FILE* file, uint64_t position, Mp3FrameInfo& frame) {
    std::array<uint8_t, 4> bytes = {};
    return fseek(file, static_cast<long>(position), SEEK_SET) == 0 &&
           fread(bytes.data(), 1, bytes.size(), file) == bytes.size() &&
           ParseMp3FrameHeader(bytes.data(), frame);
}

bool FindMp3Frame(FILE* file, uint64_t start, uint64_t end, uint64_t max_scan,
                  const Mp3FrameInfo* expected, std::array<uint8_t, 1024>& buffer,
                  uint64_t& frame_position, Mp3FrameInfo& frame) {
    const uint64_t scan_end = std::min(end, start + max_scan);
    uint64_t chunk_start = start;
    while (chunk_start + 4 <= scan_end) {
        const size_t bytes_to_read =
            static_cast<size_t>(std::min<uint64_t>(buffer.size(), scan_end - chunk_start));
        if (fseek(file, static_cast<long>(chunk_start), SEEK_SET) != 0 ||
            fread(buffer.data(), 1, bytes_to_read, file) != bytes_to_read)
            return false;

        for (size_t offset = 0; offset + 4 <= bytes_to_read; ++offset) {
            Mp3FrameInfo candidate;
            if (!ParseMp3FrameHeader(buffer.data() + offset, candidate) ||
                (expected && !SameMp3Stream(*expected, candidate)))
                continue;
            const uint64_t candidate_position = chunk_start + offset;
            const uint64_t next_position = candidate_position + candidate.frame_size;
            if (next_position + 4 > end)
                continue;
            Mp3FrameInfo next_frame;
            if (!ReadMp3FrameAt(file, next_position, next_frame) ||
                !SameMp3Stream(candidate, next_frame))
                continue;
            frame_position = candidate_position;
            frame = candidate;
            return true;
        }

        if (bytes_to_read <= 3)
            break;
        chunk_start += bytes_to_read - 3;
    }
    return false;
}

uint32_t DurationFromMp3Frames(uint32_t frame_count, const Mp3FrameInfo& frame) {
    if (frame_count == 0 || frame.sample_rate == 0)
        return 0;
    const uint64_t duration_ms =
        static_cast<uint64_t>(frame_count) * frame.samples_per_frame * 1000 / frame.sample_rate;
    return duration_ms <= 24ULL * 60 * 60 * 1000 ? static_cast<uint32_t>(duration_ms) : 0;
}

uint32_t ReadBigEndian32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

uint32_t ReadMp3Duration(const std::string& full_path, uint64_t file_size) {
    if (file_size < 8 || file_size > SdStorage::kMaxFileBytes)
        return 0;
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full_path.c_str(), "rb"), fclose);
    if (!file)
        return 0;

    uint64_t audio_end = file_size;
    if (audio_end >= 128) {
        std::array<uint8_t, 3> id3v1 = {};
        if (fseek(file.get(), static_cast<long>(audio_end - 128), SEEK_SET) == 0 &&
            fread(id3v1.data(), 1, id3v1.size(), file.get()) == id3v1.size() &&
            std::memcmp(id3v1.data(), "TAG", 3) == 0)
            audio_end -= 128;
    }

    uint64_t audio_start = 0;
    std::array<uint8_t, 10> id3_header = {};
    if (fseek(file.get(), 0, SEEK_SET) == 0 &&
        fread(id3_header.data(), 1, id3_header.size(), file.get()) == id3_header.size() &&
        std::memcmp(id3_header.data(), "ID3", 3) == 0) {
        const uint32_t tag_size = ReadSynchsafe(id3_header.data() + 6);
        if (tag_size > audio_end || static_cast<uint64_t>(tag_size) + 10 > audio_end)
            return 0;
        audio_start = static_cast<uint64_t>(tag_size) + 10;
    }
    if (audio_start >= audio_end)
        return 0;

    std::array<uint8_t, 1024> scan_buffer = {};
    uint64_t first_position = 0;
    Mp3FrameInfo first_frame;
    if (!FindMp3Frame(file.get(), audio_start, audio_end, 64 * 1024, nullptr, scan_buffer,
                      first_position, first_frame))
        return 0;

    // Prefer exact frame counts embedded by common VBR/CBR encoders.
    const uint32_t side_info_size =
        first_frame.version_id == 3 ? (first_frame.mono ? 17 : 32) : (first_frame.mono ? 9 : 17);
    const uint64_t xing_offset =
        first_position + 4 + (first_frame.has_crc ? 2 : 0) + side_info_size;
    std::array<uint8_t, 12> xing = {};
    if (xing_offset + xing.size() <= first_position + first_frame.frame_size &&
        fseek(file.get(), static_cast<long>(xing_offset), SEEK_SET) == 0 &&
        fread(xing.data(), 1, xing.size(), file.get()) == xing.size() &&
        (std::memcmp(xing.data(), "Xing", 4) == 0 || std::memcmp(xing.data(), "Info", 4) == 0)) {
        const uint32_t flags = ReadBigEndian32(xing.data() + 4);
        if (flags & 0x01) {
            const uint32_t duration =
                DurationFromMp3Frames(ReadBigEndian32(xing.data() + 8), first_frame);
            if (duration != 0)
                return duration;
        }
    }

    // CBR files can be timed from their audio byte count and frame bitrate.
    // For VBR files without a frame-count header, sparse frame probes estimate
    // the time-weighted bitrate without reading the full audio payload.
    const uint64_t audio_bytes = audio_end - first_position;
    if (audio_bytes == 0 || first_frame.bitrate_kbps == 0)
        return 0;
    constexpr size_t kProbeCount = 16;
    std::array<uint32_t, kProbeCount + 1> bitrates{};
    size_t bitrate_count = 1;
    bitrates[0] = first_frame.bitrate_kbps;
    for (size_t i = 1; i <= kProbeCount; ++i) {
        const uint64_t target = first_position + audio_bytes * i / (kProbeCount + 1);
        uint64_t sample_position = 0;
        Mp3FrameInfo sample;
        if (FindMp3Frame(file.get(), target, audio_end, 4096, &first_frame, scan_buffer,
                         sample_position, sample))
            bitrates[bitrate_count++] = sample.bitrate_kbps;
    }

    uint32_t bitrate_kbps = first_frame.bitrate_kbps;
    bool constant_bitrate = true;
    for (size_t i = 1; i < bitrate_count; ++i) {
        if (bitrates[i] != bitrates[0]) {
            constant_bitrate = false;
            break;
        }
    }
    if (!constant_bitrate) {
        if (bitrate_count < 4)
            return 0;
        uint64_t reciprocal_sum = 0;
        for (size_t i = 0; i < bitrate_count; ++i)
            reciprocal_sum += 1000000ULL / bitrates[i];
        if (reciprocal_sum == 0)
            return 0;
        bitrate_kbps = static_cast<uint32_t>(bitrate_count * 1000000ULL / reciprocal_sum);
    }

    const uint64_t duration_ms = audio_bytes * 8 / bitrate_kbps;
    return duration_ms <= 24ULL * 60 * 60 * 1000 ? static_cast<uint32_t>(duration_ms) : 0;
}
}  // namespace

SdStorage::~SdStorage() {
    if (worker_ != nullptr) {
        xTaskNotify(worker_, kStopCommand, eSetValueWithOverwrite);
        xSemaphoreTake(stopped_, portMAX_DELAY);
    }
    if (stopped_)
        vSemaphoreDelete(stopped_);
    InvalidatePhotoIndex();
    heap_caps_free(music_index_);
}

void SdStorage::Start() {
    if (worker_ != nullptr)
        return;
    stopped_ = xSemaphoreCreateBinary();
    if (!stopped_) {
        SetState("error", "Không đủ bộ nhớ để khởi tạo microSD.");
        return;
    }
    if (xTaskCreate(Worker, "robot_sd", kWorkerStackBytes, this, 1, &worker_) != pdPASS) {
        vSemaphoreDelete(stopped_);
        stopped_ = nullptr;
        SetState("error", "Không tạo được task microSD.");
        return;
    }
    Request("mount");
}

void SdStorage::Worker(void* context) {
    auto& self = *static_cast<SdStorage*>(context);
    while (true) {
        uint32_t command = 0;
        xTaskNotifyWait(0, UINT32_MAX, &command, portMAX_DELAY);
        {
            std::lock_guard<std::mutex> lock(self.io_mutex_);
            if (command == kStopCommand) {
                self.Unmount();
                break;
            }
            if (command == kMountCommand)
                self.Mount();
            else if (command == kUnmountCommand)
                self.Unmount();
            else if (command == kRefreshCommand)
                self.RefreshSpace();
            else if (command == kMusicCommand)
                self.InvalidateMusicIndex();
            if (self.card_ && self.music_dirty_ && self.playback_files_.load() == 0 &&
                self.upload_path_.empty()) {
                self.BuildMusicIndex();
                ESP_LOGI(kTag, "SD worker stack low-water mark: %u bytes",
                         unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            }
        }
    }
    xSemaphoreGive(self.stopped_);
    vTaskDelete(nullptr);
}

SdStorage::Status SdStorage::GetStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

void SdStorage::SetState(const char* state, const std::string& message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = state;
    status_.message = message;
    if (status_.state != "mounted")
        status_.space_valid = false;
}

SdStorage::Result SdStorage::Request(const std::string& action) {
    uint32_t command = action == "mount"           ? kMountCommand
                       : action == "unmount"       ? kUnmountCommand
                       : action == "refresh"       ? kRefreshCommand
                       : action == "music_reindex" ? kMusicCommand
                                                   : 0;
    if (!command)
        return {Code::Invalid, "Thao tác không hợp lệ."};
    if (command == kUnmountCommand && playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi mới tháo an toàn."};
    if (!worker_)
        return {Code::Unavailable, "Task microSD chưa sẵn sàng."};
    if (xTaskNotify(worker_, command, eSetValueWithoutOverwrite) != pdPASS)
        return {Code::Busy, "MicroSD đang có yêu cầu chờ xử lý."};
    return {};
}

void SdStorage::Mount() {
    if (card_ && GetStatus().state == "error") {
        Unmount();
        if (card_)
            return;
    }
    if (card_) {
        SetState("mounted", "Thẻ đã được kết nối.");
        RefreshSpace();
        return;
    }
    SetState("mounting", "Đang kết nối thẻ nhớ...");
    if (!bus_initialized_) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = SD_MOSI_PIN;
        bus.miso_io_num = SD_MISO_PIN;
        bus.sclk_io_num = SD_SCK_PIN;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = kChunkBytes;
        auto err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            SetState("error", std::string("Không khởi tạo được SPI2: ") + esp_err_to_name(err));
            ESP_LOGW(kTag, "SPI2 init failed: %s", esp_err_to_name(err));
            return;
        }
        bus_initialized_ = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SD_CS_PIN;
    device.gpio_cd = GPIO_NUM_NC;
    device.gpio_wp = GPIO_NUM_NC;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 4;
    sdmmc_card_t* card = nullptr;
    auto err = esp_vfs_fat_sdspi_mount(kMount, &host, &device, &mount, &card);
    if (err != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        bus_initialized_ = false;
        SetState("error", std::string("Không đọc được thẻ: ") + esp_err_to_name(err) +
                              ". Kiểm tra thẻ FAT32, dây nối và nguồn cấp.");
        ESP_LOGW(kTag, "SD mount failed: %s (CS42/SCK41/MOSI40/MISO39)", esp_err_to_name(err));
        return;
    }
    card_ = card;
    InvalidatePhotoIndex();
    SetState("mounted", "MicroSD sẵn sàng.");
    InvalidateMusicIndex();
    RefreshSpace();
    ESP_LOGI(kTag, "SD ready: %s, SPI2 10MHz", card->cid.name);
}

void SdStorage::Unmount() {
    if (playback_files_.load() != 0) {
        SetState("mounted", "Dừng phát nhạc từ thẻ trước khi tháo microSD an toàn.");
        return;
    }
    if (!upload_path_.empty()) {
        // Only our temporary file is removed; the destination was never replaced.
        std::string full;
        if (Resolve(upload_path_, full, false))
            std::remove((full + kTemporarySuffix).c_str());
        ClearUpload();
    }
    if (card_) {
        auto err = esp_vfs_fat_sdcard_unmount(kMount, static_cast<sdmmc_card_t*>(card_));
        if (err != ESP_OK) {
            SetState("error", std::string("Không tháo được thẻ: ") + esp_err_to_name(err));
            return;
        }
        card_ = nullptr;
    }
    if (bus_initialized_) {
        spi_bus_free(SPI2_HOST);
        bus_initialized_ = false;
    }
    InvalidatePhotoIndex();
    SetState("unmounted", "Đã ngắt kết nối. Có thể tháo thẻ.");
    {
        std::lock_guard<std::mutex> music_lock(music_mutex_);
        heap_caps_free(music_index_);
        music_index_ = nullptr;
        music_count_ = 0;
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        status_.music_index_state = "unavailable";
        status_.music_tracks = 0;
        status_.music_truncated = false;
    }
}

void SdStorage::RefreshSpace() {
    if (!card_)
        return;
    uint64_t total = 0, available = 0;
    auto err = esp_vfs_fat_info(kMount, &total, &available);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.space_valid = err == ESP_OK;
    if (err == ESP_OK) {
        status_.total_bytes = total;
        status_.free_bytes = available;
    }
}

void SdStorage::InvalidateSpace() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.space_valid = false;
}

void SdStorage::ClearUpload() {
    upload_path_.clear();
    upload_total_ = upload_offset_ = 0;
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.upload_path.clear();
    status_.uploaded_bytes = 0;
}

SdStorage::Result SdStorage::Ready() const {
    if (!card_ || GetStatus().state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng. Kiểm tra thẻ rồi bấm Kết nối lại."};
    return {};
}

SdStorage::Result SdStorage::IoError(const char* operation) {
    const int error = errno;
    ESP_LOGW(kTag, "%s failed, errno=%d (%s)", operation, error, strerror(error));
    if (error == ENOSPC)
        return {Code::Limit, "Thẻ đã đầy. Hủy upload rồi xóa file để giải phóng dung lượng."};
    if (error == ENOENT || error == ENOTDIR)
        return {Code::NotFound, "Không tìm thấy đường dẫn hoặc thư mục cha."};
    if (error == EEXIST)
        return {Code::Exists, "Tên này đã tồn tại."};
    if (error == EBUSY || error == EMFILE || error == ENFILE)
        return {Code::Busy, "File đang bận. Hãy hoàn tất thao tác hiện tại trước."};
    // FatFs maps FR_DENIED to EACCES. That includes a non-empty directory
    // passed to rmdir(), a read-only item, and a full directory table; these
    // are operation failures, not evidence that the card has become unusable.
    if (error == EACCES || error == EPERM || error == EROFS)
        return {Code::Invalid,
                "Thẻ từ chối thay đổi. Kiểm tra thư mục có rỗng không, thuộc tính chỉ đọc và dung "
                "lượng."};
    SetState("error", "Lỗi đọc/ghi microSD. Tháo an toàn rồi kết nối lại.");
    return {Code::Io, "Lỗi đọc/ghi microSD."};
}

SdStorage::Result SdStorage::Resolve(const std::string& path, std::string& full, bool allow_root) {
    if (path.empty() || path.size() > 192 || path.front() != '/' || !ValidUtf8(path) ||
        (!allow_root && path == "/"))
        return {Code::Invalid, "Đường dẫn không hợp lệ."};
    if (path != "/") {
        size_t begin = 1;
        while (begin < path.size()) {
            size_t end = path.find('/', begin);
            if (end == std::string::npos)
                end = path.size();
            const std::string part = path.substr(begin, end - begin);
            std::string folded = part;
            for (char& c : folded) {
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            }
            // Allow dotfiles such as .metadata.json in the SD manager, while
            // still blocking traversal and our private in-progress upload files.
            if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
                part.back() == ' ' || part.size() > 120 ||
                folded.find(kTemporarySuffix) != std::string::npos)
                return {Code::Invalid, "Tên file/thư mục không hợp lệ hoặc được dành riêng."};
            begin = end + 1;
        }
        if (path.back() == '/')
            return {Code::Invalid, "Đường dẫn không được kết thúc bằng /."};
    }
    for (unsigned char c : path) {
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '<' ||
            c == '>' || c == '|' || c == '"')
            return {Code::Invalid, "Đường dẫn chứa ký tự không hợp lệ."};
    }
    full = std::string(kMount) + (path == "/" ? "" : path);
    return {};
}

SdStorage::Result SdStorage::List(const std::string& path, size_t offset, Listing& listing) {
    std::string full;
    if (auto result = Resolve(path, full); !result)
        return result;
    if (offset > 10000)
        return {Code::Limit, "Thư mục vượt giới hạn phân trang."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    DIR* raw = opendir(full.c_str());
    if (!raw)
        return errno == ENOENT ? Result{Code::NotFound, "Không tìm thấy thư mục."}
                               : IoError("opendir");
    std::unique_ptr<DIR, decltype(&closedir)> dir(raw, closedir);
    listing = {};
    size_t index = 0;
    size_t scanned = 0;
    errno = 0;
    while (auto* item = readdir(dir.get())) {
        if (++scanned % 32 == 0)
            vTaskDelay(1);
        std::string child;
        if (!Resolve((path == "/" ? "" : path) + "/" + item->d_name, child, false))
            continue;
        if (index++ < offset) {
            if (index % 32 == 0)
                vTaskDelay(1);
            continue;
        }
        if (listing.entries.size() == kPageSize) {
            listing.more = true;
            break;
        }
        struct stat info = {};
        if (stat(child.c_str(), &info) != 0)
            return IoError("stat");
        listing.entries.push_back(
            {item->d_name, S_ISDIR(info.st_mode), static_cast<uint64_t>(info.st_size)});
        errno = 0;
    }
    if (errno)
        return IoError("readdir");
    listing.next_offset = offset + listing.entries.size();
    return {};
}

void SdStorage::InvalidateMusicIndex() {
    music_dirty_ = true;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.music_index_state = "dirty";
        status_.music_index_message = "Đang chờ cập nhật thư viện nhạc.";
    }
    if (worker_)
        xTaskNotify(worker_, 0, eNoAction);
}

void SdStorage::BuildMusicIndex() {
    // No audio file is open while this worker holds io_mutex_. Search only
    // touches the published RAM snapshot and never takes the card I/O lock.
    music_dirty_ = false;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.music_index_state = "indexing";
        status_.music_index_message.clear();
    }
    const auto fail = [this](const char* message) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.music_index_state = "error";
        status_.music_index_message = message;
    };
    for (const char* folder : {"/sdcard/robot", "/sdcard/robot/music"}) {
        if (mkdir(folder, 0775) != 0 && errno != EEXIST) {
            fail("Không tạo được /robot/music. Kiểm tra quyền ghi và dung lượng thẻ.");
            return;
        }
    }
    using IndexOwner = std::unique_ptr<MusicIndexEntry, decltype(&heap_caps_free)>;
    IndexOwner next(
        static_cast<MusicIndexEntry*>(heap_caps_calloc(kMaxMusicTracks, sizeof(MusicIndexEntry),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        heap_caps_free);
    if (!next) {
        fail("Không đủ PSRAM để tạo thư viện nhạc.");
        return;
    }
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/sdcard/robot/music"), closedir);
    if (!dir) {
        fail("Không đọc được /robot/music.");
        return;
    }
    std::array<uint8_t, 1024> tag;
    size_t count = 0, visited = 0;
    bool truncated = false;
    while (true) {
        errno = 0;
        auto* item = readdir(dir.get());
        if (!item) {
            if (errno) {
                fail("Lỗi đọc thư mục nhạc; hãy thử cập nhật lại thư viện.");
                return;
            }
            break;
        }
        if (item->d_name[0] == '.')
            continue;
        if (++visited > 4096) {
            truncated = true;
            break;
        }
        if (visited % 16 == 0)
            vTaskDelay(1);
        std::string path = std::string(kMusicRoot) + "/" + item->d_name;
        std::string full;
        if (!HasAudioExtension(path) || !Resolve(path, full, false))
            continue;
        struct stat info = {};
        if (stat(full.c_str(), &info) != 0) {
            fail("Lỗi đọc thông tin file nhạc; hãy cập nhật lại thư viện.");
            return;
        }
        if (!S_ISREG(info.st_mode) || info.st_size <= 0)
            continue;
        if (count == kMaxMusicTracks) {
            truncated = true;
            break;
        }
        MusicTrack track;
        track.path = path;
        ReadId3Metadata(full, track, tag.data(), tag.size());
        if (track.duration_ms == 0 && FoldText(path.substr(path.find_last_of('.'))) == ".mp3")
            track.duration_ms = ReadMp3Duration(full, info.st_size);
        auto& entry = next.get()[count++];
        CopyMusicText(entry.path, sizeof(entry.path), path);
        CopyMusicText(entry.title, sizeof(entry.title), track.title);
        CopyMusicText(entry.artist, sizeof(entry.artist), track.artist);
        CopyMusicText(entry.album, sizeof(entry.album), track.album);
        CopyMusicText(entry.genre, sizeof(entry.genre), track.genre);
        entry.size = info.st_size;
        entry.duration_ms = track.duration_ms;
    }
    bool metadata_warning = false;
    ApplyMusicMetadataOverrides(next.get(), count, metadata_warning);
    {
        std::lock_guard<std::mutex> lock(music_mutex_);
        heap_caps_free(music_index_);
        music_index_ = next.release();
        music_count_ = count;
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        status_.music_index_state = "ready";
        status_.music_tracks = count;
        status_.music_truncated = truncated;
        status_.music_generation++;
        status_.music_index_message =
            truncated ? "Thư viện vượt giới hạn 512 bài hoặc 4096 mục; một số bài chưa được index."
                      : "Thư viện nhạc sẵn sàng.";
    }
    if (metadata_warning) {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        status_.music_index_message = "Th? vi?n s?n s�ng; c� metadata s?a tay kh�ng d?c du?c.";
        ESP_LOGW(kTag, "Some music metadata overrides could not be read");
    }
    ESP_LOGI(kTag, "Music index ready: %u tracks, truncated=%d", unsigned(count), truncated);
}

void SdStorage::ApplyMusicMetadataOverrides(MusicIndexEntry* entries, size_t count, bool& warning) {
    struct stat info = {};
    if (stat(kMusicMetadataPath, &info) != 0) {
        if (errno != ENOENT) {
            warning = true;
            return;
        }
        if (stat(kMusicMetadataBackupPath, &info) == 0) {
            if (rename(kMusicMetadataBackupPath, kMusicMetadataPath) != 0) {
                warning = true;
                return;
            }
        } else if (errno != ENOENT) {
            warning = true;
            return;
        }
    }

    FILE* raw = fopen(kMusicMetadataPath, "rb");
    if (!raw) {
        if (errno != ENOENT)
            warning = true;
        unlink(kMusicMetadataTemporaryPath);
        return;
    }
    std::unique_ptr<FILE, decltype(&fclose)> file(raw, fclose);
    std::array<char, kMusicMetadataLineBytes> line = {};
    size_t records = 0;
    while (fgets(line.data(), line.size(), file.get())) {
        const size_t length = std::strlen(line.data());
        if (length == 0)
            continue;
        if (line[length - 1] != '\n' && !feof(file.get())) {
            warning = true;
            int c;
            while ((c = fgetc(file.get())) != '\n' && c != EOF) {
            }
            continue;
        }
        auto parse_line = line;
        std::string path, artist, album, genre;
        bool has_path = false, has_artist = false, has_album = false, has_genre = false;
        bool has_version = false, is_object = false;
        if (!ParseMusicMetadataLine(parse_line.data(), path, artist, album, genre, has_path,
                                    has_artist, has_album, has_genre, has_version, is_object)) {
            warning = true;
            continue;
        }
        if (!is_object || !has_path)
            continue;
        if (++records > kMaxMusicTracks) {
            warning = true;
            continue;
        }
        for (size_t i = 0; i < count; ++i) {
            if (path != entries[i].path)
                continue;
            if (has_artist)
                CopyMusicText(entries[i].artist, sizeof(entries[i].artist), artist);
            if (has_album)
                CopyMusicText(entries[i].album, sizeof(entries[i].album), album);
            if (has_genre)
                CopyMusicText(entries[i].genre, sizeof(entries[i].genre), genre);
            break;
        }
    }
    if (ferror(file.get()))
        warning = true;
    file.reset();
    if (!warning) {
        unlink(kMusicMetadataBackupPath);
        unlink(kMusicMetadataTemporaryPath);
    }
}

SdStorage::Result SdStorage::WriteMusicMetadataOverrides(const std::string& path,
                                                         const std::string& artist,
                                                         const std::string& album,
                                                         const std::string& genre, bool reset) {
    FILE* source_raw = fopen(kMusicMetadataPath, "rb");
    if (!source_raw && errno != ENOENT)
        return IoError("open music metadata sidecar");
    std::unique_ptr<FILE, decltype(&fclose)> source(source_raw, fclose);
    FILE* target_raw = fopen(kMusicMetadataTemporaryPath, "wb");
    if (!target_raw)
        return IoError("create music metadata sidecar");
    std::unique_ptr<FILE, decltype(&fclose)> target(target_raw, fclose);

    bool first = true;
    bool write_ok = fputs("[\n", target.get()) >= 0 &&
                    WriteMusicMetadataObject(target.get(), "{\"version\":2}", first);
    std::array<char, kMusicMetadataLineBytes> line = {};
    while (write_ok && source && fgets(line.data(), line.size(), source.get())) {
        const size_t length = std::strlen(line.data());
        if (length == 0)
            continue;
        if (line[length - 1] != '\n' && !feof(source.get())) {
            write_ok = false;
            errno = EOVERFLOW;
            break;
        }
        auto parse_line = line;
        std::string record_path, record_artist, record_album, record_genre;
        bool has_path = false, has_artist = false, has_album = false, has_genre = false;
        bool has_version = false, is_object = false;
        if (!ParseMusicMetadataLine(parse_line.data(), record_path, record_artist, record_album,
                                    record_genre, has_path, has_artist, has_album, has_genre,
                                    has_version, is_object)) {
            write_ok = false;
            errno = EINVAL;
            break;
        }
        if (!is_object || has_version || (has_path && record_path == path))
            continue;
        char* raw_json = TrimMusicMetadataLine(line.data());
        if (*raw_json) {
            std::string compact_json;
            if (!CompactMusicMetadataRecord(raw_json, compact_json)) {
                write_ok = false;
                errno = ENOMEM;
            } else if (!WriteMusicMetadataObject(target.get(), compact_json.c_str(), first)) {
                write_ok = false;
            }
        }
    }
    if (source && ferror(source.get()))
        write_ok = false;
    if (write_ok && !reset &&
        !WriteMusicMetadataRecord(target.get(), path, artist, album, genre, first))
        write_ok = false;
    if (write_ok && fputs("\n]\n", target.get()) < 0)
        write_ok = false;
    if (write_ok && (fflush(target.get()) != 0 || fsync(fileno(target.get())) != 0))
        write_ok = false;
    const int saved_error = errno;
    source.reset();
    target.reset();
    if (!write_ok) {
        unlink(kMusicMetadataTemporaryPath);
        errno = saved_error ? saved_error : EIO;
        if (errno == EINVAL)
            return {Code::Invalid,
                    "File .metadata.json bị lỗi; không ghi đè để tránh mất dữ liệu."};
        if (errno == EOVERFLOW)
            return {Code::Limit, "Một dòng trong .metadata.json vượt giới hạn an toàn."};
        return IoError("write music metadata sidecar");
    }

    struct stat info = {};
    const bool had_previous = stat(kMusicMetadataPath, &info) == 0;
    if (!had_previous && errno != ENOENT) {
        unlink(kMusicMetadataTemporaryPath);
        return IoError("stat music metadata sidecar");
    }
    if (unlink(kMusicMetadataBackupPath) != 0 && errno != ENOENT) {
        unlink(kMusicMetadataTemporaryPath);
        return IoError("remove old music metadata backup");
    }
    if (had_previous && rename(kMusicMetadataPath, kMusicMetadataBackupPath) != 0) {
        unlink(kMusicMetadataTemporaryPath);
        return IoError("backup music metadata sidecar");
    }
    if (rename(kMusicMetadataTemporaryPath, kMusicMetadataPath) != 0) {
        const int rename_error = errno;
        if (had_previous)
            rename(kMusicMetadataBackupPath, kMusicMetadataPath);
        errno = rename_error;
        return IoError("commit music metadata sidecar");
    }
    if (had_previous && unlink(kMusicMetadataBackupPath) != 0)
        ESP_LOGW(kTag, "Could not remove stale music metadata backup, errno=%d", errno);
    InvalidateSpace();
    return {};
}

SdStorage::Result SdStorage::SetMusicMetadata(const std::string& path, const std::string& artist,
                                              const std::string& album, const std::string& genre,
                                              bool reset) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    if (path.compare(0, sizeof(kMusicRoot) - 1, kMusicRoot) != 0 ||
        path.size() <= sizeof(kMusicRoot) || path[sizeof(kMusicRoot) - 1] != '/' ||
        !HasAudioExtension(path))
        return {Code::Invalid, "Chỉ chỉnh metadata file nhạc trực tiếp trong /robot/music."};
    const std::string clean_artist = TrimText(artist);
    const std::string clean_album = TrimText(album);
    const std::string clean_genre = TrimText(genre);
    if (!reset && (!ValidMusicMetadataValue(clean_artist) ||
                   !ValidMusicMetadataValue(clean_album) || !ValidMusicMetadataValue(clean_genre)))
        return {Code::Invalid, "Mỗi trường metadata phải là UTF-8 hợp lệ, tối đa 96 byte."};

    std::unique_lock<std::mutex> io_lock(io_mutex_, std::try_to_lock);
    if (!io_lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (!upload_path_.empty())
        return {Code::Busy, "Hãy hoàn tất hoặc hủy upload trước."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return {Code::NotFound, "Không tìm thấy file nhạc trong thư viện."};

    bool found = false;
    {
        std::lock_guard<std::mutex> music_lock(music_mutex_);
        const auto status = GetStatus();
        if (status.music_index_state != "ready")
            return {Code::Busy, "Thư viện nhạc đang cập nhật; hãy thử lại sau."};
        for (size_t i = 0; i < music_count_; ++i) {
            if (path == music_index_[i].path) {
                found = true;
                break;
            }
        }
    }
    if (!found)
        return {Code::NotFound, "Bài hát không còn trong index; hãy cập nhật thư viện."};

    if (auto result =
            WriteMusicMetadataOverrides(path, clean_artist, clean_album, clean_genre, reset);
        !result)
        return result;
    if (reset) {
        InvalidateMusicIndex();
        return {};
    }

    std::lock_guard<std::mutex> music_lock(music_mutex_);
    for (size_t i = 0; i < music_count_; ++i) {
        auto& entry = music_index_[i];
        if (path != entry.path)
            continue;
        CopyMusicText(entry.artist, sizeof(entry.artist), clean_artist);
        CopyMusicText(entry.album, sizeof(entry.album), clean_album);
        CopyMusicText(entry.genre, sizeof(entry.genre), clean_genre);
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        ++status_.music_generation;
        status_.music_index_message = "Metadata sửa tay đã được lưu trên thẻ.";
        return {};
    }
    return {Code::NotFound, "Bài hát không còn trong index."};
}

SdStorage::Result SdStorage::SearchMusic(const std::string& query, const std::string& artist,
                                         const std::string& album, const std::string& genre,
                                         size_t limit, std::vector<MusicTrack>& tracks,
                                         bool& truncated, size_t offset, const std::string& sort,
                                         bool descending, const std::string& missing,
                                         size_t* total_matches) {
    tracks.clear();
    truncated = false;
    if (total_matches)
        *total_matches = 0;
    const bool valid_sort = sort == "relevance" || sort == "title" || sort == "artist" ||
                            sort == "album" || sort == "genre" || sort == "duration";
    const bool valid_missing = missing.empty() || missing == "any" || missing == "artist" ||
                               missing == "album" || missing == "genre";
    if (limit == 0 || limit > 50 || offset > kMaxMusicTracks || !valid_sort || !valid_missing ||
        query.size() > 96 || artist.size() > 96 || album.size() > 96 || genre.size() > 96 ||
        !ValidUtf8(query) || !ValidUtf8(artist) || !ValidUtf8(album) || !ValidUtf8(genre))
        return {Code::Invalid,
                "Bộ lọc phải là UTF-8, tối đa 96 byte; limit 1-50, sort/missing hợp lệ."};
    const auto q = MusicKey(query), a = MusicKey(artist), al = MusicKey(album), g = GenreKey(genre);
    std::lock_guard<std::mutex> lock(music_mutex_);
    const auto status = GetStatus();
    if (status.state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (status.music_index_state != "ready")
        return {Code::Busy, "Thư viện chưa sẵn sàng: " + status.music_index_message};
    using Match = std::pair<int, size_t>;
    std::vector<Match> matches;
    matches.reserve(music_count_);
    const auto contains = [](const std::string& value, const std::string& key) {
        return key.empty() || value.find(key) != std::string::npos;
    };
    for (size_t i = 0; i < music_count_; ++i) {
        const auto& item = music_index_[i];
        const auto title = MusicKey(item.title), performer = MusicKey(item.artist),
                   record = MusicKey(item.album), category = GenreKey(item.genre),
                   path = MusicKey(item.path);
        if (!contains(performer, a) || !contains(record, al) || !contains(category, g))
            continue;
        if ((missing == "any" && item.artist[0] && item.album[0] && item.genre[0]) ||
            (missing == "artist" && item.artist[0]) || (missing == "album" && item.album[0]) ||
            (missing == "genre" && item.genre[0]))
            continue;
        int score = q.empty()                                      ? 0
                    : title == q                                   ? 100
                    : title.find(q) == 0                           ? 80
                    : contains(title, q)                           ? 60
                    : contains(performer, q)                       ? 40
                    : contains(record, q) || contains(category, q) ? 20
                    : contains(path, q)                            ? 10
                                                                   : -1;
        if (score < 0)
            continue;
        matches.emplace_back(-score, i);
    }

    const auto compare_text = [this, &sort, descending](size_t left, size_t right) {
        const auto value = [this, &sort](size_t index) -> const char* {
            const auto& item = music_index_[index];
            if (sort == "title")
                return item.title;
            if (sort == "artist")
                return item.artist;
            if (sort == "album")
                return item.album;
            return item.genre;
        };
        const auto left_key = sort == "genre" ? GenreKey(value(left)) : MusicKey(value(left));
        const auto right_key = sort == "genre" ? GenreKey(value(right)) : MusicKey(value(right));
        if (left_key != right_key)
            return descending ? left_key > right_key : left_key < right_key;
        return std::strcmp(music_index_[left].path, music_index_[right].path) < 0;
    };
    std::sort(matches.begin(), matches.end(),
              [this, &sort, descending, &compare_text](const Match& left, const Match& right) {
                  if (sort == "relevance" && left.first != right.first)
                      return descending ? left.first > right.first : left.first < right.first;
                  if (sort == "duration") {
                      const auto left_duration = music_index_[left.second].duration_ms;
                      const auto right_duration = music_index_[right.second].duration_ms;
                      if (left_duration != right_duration)
                          return descending ? left_duration > right_duration
                                            : left_duration < right_duration;
                  } else if (sort != "relevance") {
                      if (compare_text(left.second, right.second))
                          return true;
                      if (compare_text(right.second, left.second))
                          return false;
                  }
                  return std::strcmp(music_index_[left.second].path,
                                     music_index_[right.second].path) < 0;
              });

    if (total_matches)
        *total_matches = matches.size();
    const auto end = std::min(matches.size(), offset + limit);
    for (size_t i = std::min(offset, matches.size()); i < end; ++i) {
        const auto& match = matches[i];
        const auto& item = music_index_[match.second];
        tracks.push_back({item.path, item.title, item.artist, item.album, item.genre, item.size,
                          item.duration_ms});
    }
    truncated = status.music_truncated || end < matches.size();
    return {};
}

SdStorage::Result SdStorage::GetAlbumMusicTracks(const std::string& album,
                                                 std::vector<MusicTrack>& tracks) {
    tracks.clear();
    if (album.empty() || album.size() > 96 || !ValidUtf8(album))
        return {Code::Invalid, "Tên album phải là UTF-8, dài 1-96 byte."};
    const auto key = MusicKey(album);
    std::lock_guard<std::mutex> lock(music_mutex_);
    const auto status = GetStatus();
    if (status.state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (status.music_index_state != "ready")
        return {Code::Busy, "Thư viện nhạc chưa sẵn sàng: " + status.music_index_message};
    if (status.music_truncated)
        return {Code::Limit, "Thư viện bị giới hạn 512 bài; không thể đảm bảo phát đủ album."};

    for (size_t i = 0; i < music_count_; ++i) {
        const auto& item = music_index_[i];
        if (MusicKey(item.album) != key)
            continue;
        tracks.push_back({item.path, item.title, item.artist, item.album, item.genre, item.size,
                          item.duration_ms});
    }
    if (tracks.empty()) {
        std::string matched_album;
        for (size_t i = 0; i < music_count_; ++i) {
            const auto& item = music_index_[i];
            const auto item_album = MusicKey(item.album);
            if (item_album.find(key) == std::string::npos)
                continue;
            if (!matched_album.empty() && matched_album != item_album)
                return {Code::Invalid, "Tên album khớp nhiều album; hãy nêu tên đầy đủ."};
            matched_album = item_album;
        }
        if (!matched_album.empty()) {
            for (size_t i = 0; i < music_count_; ++i) {
                const auto& item = music_index_[i];
                if (MusicKey(item.album) != matched_album)
                    continue;
                tracks.push_back({item.path, item.title, item.artist, item.album, item.genre,
                                  item.size, item.duration_ms});
            }
        }
    }
    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack& left, const MusicTrack& right) {
        return left.path < right.path;
    });
    if (tracks.empty())
        return {Code::NotFound, "Không tìm thấy bài nào thuộc album này."};
    return {};
}

SdStorage::Result SdStorage::GetArtistMusicTracks(const std::string& artist,
                                                  std::vector<MusicTrack>& tracks) {
    tracks.clear();
    if (artist.empty() || artist.size() > 96 || !ValidUtf8(artist))
        return {Code::Invalid, "Tên ca sĩ phải là UTF-8, dài 1-96 byte."};
    const auto key = MusicKey(artist);
    std::lock_guard<std::mutex> lock(music_mutex_);
    const auto status = GetStatus();
    if (status.state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (status.music_index_state != "ready")
        return {Code::Busy, "Thư viện nhạc chưa sẵn sàng: " + status.music_index_message};
    if (status.music_truncated)
        return {Code::Limit,
                "Thư viện bị giới hạn 512 bài; không thể đảm bảo phát đủ bài của ca sĩ."};

    for (size_t i = 0; i < music_count_; ++i) {
        const auto& item = music_index_[i];
        if (MusicKey(item.artist).find(key) == std::string::npos)
            continue;
        tracks.push_back({item.path, item.title, item.artist, item.album, item.genre, item.size,
                          item.duration_ms});
    }
    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack& left, const MusicTrack& right) {
        return left.path < right.path;
    });
    if (tracks.empty())
        return {Code::NotFound, "Không tìm thấy bài nào của ca sĩ này."};
    return {};
}

SdStorage::Result SdStorage::NextMusicTrack(const std::string& current_path,
                                            MusicTrack& track) const {
    std::lock_guard<std::mutex> lock(music_mutex_);
    const auto status = GetStatus();
    if (status.state != "mounted")
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (status.music_index_state != "ready")
        return {Code::Busy, "Thư viện nhạc chưa sẵn sàng."};
    if (music_count_ == 0)
        return {Code::NotFound, "Thư viện chưa có bài nhạc."};

    size_t current_index = music_count_;
    for (size_t i = 0; i < music_count_; ++i) {
        if (current_path == music_index_[i].path) {
            current_index = i;
            break;
        }
    }
    if (current_index == music_count_)
        return {Code::NotFound, "Bài đang phát không còn trong thư viện."};

    const MusicIndexEntry* next = nullptr;
    const MusicIndexEntry* first = &music_index_[0];
    for (size_t i = 0; i < music_count_; ++i) {
        const auto& item = music_index_[i];
        if (std::strcmp(item.path, first->path) < 0)
            first = &item;
        if (i != current_index && std::strcmp(item.path, music_index_[current_index].path) > 0 &&
            (!next || std::strcmp(item.path, next->path) < 0))
            next = &item;
    }
    if (!next)
        next = first;
    track = {next->path,  next->title, next->artist,     next->album,
             next->genre, next->size,  next->duration_ms};
    return {};
}
void SdStorage::InvalidatePhotoIndex() {
    if (photo_index_ != nullptr)
        heap_caps_free(photo_index_);
    photo_index_ = nullptr;
    photo_count_ = 0;
    photo_index_valid_ = false;
    photo_index_truncated_ = false;
}

SdStorage::Result SdStorage::BuildPhotoIndex() {
    if (photo_index_valid_)
        return {};
    if (photo_index_ != nullptr) {
        heap_caps_free(photo_index_);
        photo_index_ = nullptr;
    }
    photo_count_ = 0;
    photo_index_truncated_ = false;
    if (auto result = Ready(); !result)
        return result;

    constexpr char kPhotoRoot[] = "/robot/images";
    constexpr size_t kMaxVisitedEntries = 2000;
    constexpr char kPhotoRootPrefix[] = "/robot/images/";
    using Directory = std::unique_ptr<DIR, decltype(&closedir)>;
    struct ScanDirectory {
        std::string path;
        Directory handle;
    };
    std::vector<ScanDirectory> directories;
    directories.reserve(9);
    DIR* root = opendir((std::string(kMount) + kPhotoRoot).c_str());
    if (root == nullptr) {
        if (errno != ENOENT)
            return IoError("open photo root");
        photo_index_valid_ = true;
        return {};
    }
    photo_index_ = static_cast<PhotoIndexEntry*>(heap_caps_calloc(
        kMaxPhotoIndexEntries, sizeof(PhotoIndexEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (photo_index_ == nullptr) {
        closedir(root);
        return {Code::Unavailable, "Không đủ PSRAM để lập chỉ mục ảnh."};
    }
    directories.push_back({kPhotoRoot, Directory(root, closedir)});
    size_t visited = 0;
    while (!directories.empty()) {
        auto& directory = directories.back();
        errno = 0;
        dirent* item = readdir(directory.handle.get());
        if (item == nullptr) {
            if (errno != 0)
                return IoError("read photo directory");
            directories.pop_back();
            continue;
        }
        if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0 ||
            item->d_name[0] == '.')
            continue;
        if (++visited > kMaxVisitedEntries) {
            photo_index_truncated_ = true;
            break;
        }
        if (visited % 32 == 0)
            vTaskDelay(1);
        const std::string path = directory.path + "/" + item->d_name;
        std::string full;
        if (!Resolve(path, full, false))
            continue;
        struct stat info = {};
        if (stat(full.c_str(), &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode)) {
            if (directories.size() < 9) {
                if (DIR* child = opendir(full.c_str()))
                    directories.push_back({path, Directory(child, closedir)});
            } else {
                photo_index_truncated_ = true;
            }
            continue;
        }
        const auto extension_pos = path.find_last_of('.');
        if (!S_ISREG(info.st_mode) || extension_pos == std::string::npos)
            continue;
        const auto extension = FoldText(path.substr(extension_pos));
        if (extension != ".jpg" && extension != ".jpeg")
            continue;
        if (photo_count_ >= kMaxPhotoIndexEntries) {
            photo_index_truncated_ = true;
            break;
        }
        if (path.size() >= sizeof(photo_index_[photo_count_].path) ||
            path.rfind(kPhotoRootPrefix, 0) != 0) {
            photo_index_truncated_ = true;
            continue;
        }
        std::memcpy(photo_index_[photo_count_].path, path.c_str(), path.size() + 1);
        photo_index_[photo_count_].size = static_cast<uint64_t>(info.st_size);
        ++photo_count_;
    }
    photo_index_valid_ = true;
    return {};
}

SdStorage::Result SdStorage::SearchPhotos(const std::string& query, const std::string& album,
                                          size_t limit, std::vector<Photo>& photos,
                                          bool& truncated) {
    if (query.size() > 96 || album.size() > 96 || limit == 0 || limit > 10 || !ValidUtf8(query) ||
        !ValidUtf8(album))
        return {Code::Invalid, "Bộ lọc ảnh không hợp lệ (giới hạn 1-10 kết quả)."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;

    photos.clear();
    truncated = photo_index_truncated_;
    const auto query_folded = FoldText(query);
    const auto album_folded = FoldText(album);
    for (size_t i = 0; i < photo_count_; ++i) {
        const auto& entry = photo_index_[i];
        const std::string path(entry.path);
        const auto slash = path.find_last_of('/');
        const auto extension_pos = path.find_last_of('.');
        Photo photo;
        photo.path = path;
        photo.size = entry.size;
        photo.name = path.substr(slash + 1, extension_pos - slash - 1);
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        if (slash > kPhotoRootLength)
            photo.album = path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1);
        const auto path_folded = FoldText(path);
        const auto photo_album = FoldText(photo.album);
        if ((!query_folded.empty() && path_folded.find(query_folded) == std::string::npos) ||
            (!album_folded.empty() && photo_album.find(album_folded) == std::string::npos))
            continue;
        if (photos.size() == limit) {
            truncated = true;
            break;
        }
        photos.push_back(photo);
    }
    return {};
}

SdStorage::Result SdStorage::RandomPhoto(const std::string& album, Photo& photo) {
    if (album.size() > 96 || !ValidUtf8(album))
        return {Code::Invalid, "Tên album không hợp lệ."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;

    const auto album_folded = FoldText(album);
    size_t matches = 0;
    for (size_t i = 0; i < photo_count_; ++i) {
        const std::string path(photo_index_[i].path);
        const auto slash = path.find_last_of('/');
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        const std::string item_album =
            slash > kPhotoRootLength
                ? path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1)
                : "";
        if (album_folded.empty() || FoldText(item_album).find(album_folded) != std::string::npos)
            ++matches;
    }
    if (matches == 0)
        return {Code::NotFound, "Không tìm thấy ảnh trong album."};
    size_t selected = esp_random() % matches;
    for (size_t i = 0; i < photo_count_; ++i) {
        const std::string path(photo_index_[i].path);
        const auto slash = path.find_last_of('/');
        constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
        const std::string item_album =
            slash > kPhotoRootLength
                ? path.substr(kPhotoRootLength + 1, slash - kPhotoRootLength - 1)
                : "";
        if (!album_folded.empty() && FoldText(item_album).find(album_folded) == std::string::npos)
            continue;
        if (selected-- == 0) {
            const auto extension_pos = path.find_last_of('.');
            photo.path = path;
            photo.size = photo_index_[i].size;
            photo.name = path.substr(slash + 1, extension_pos - slash - 1);
            photo.album = item_album;
            return {};
        }
    }
    return {Code::NotFound, "Không chọn được ảnh ngẫu nhiên."};
}

SdStorage::Result SdStorage::GetAlbumPhotoSequence(const std::string& album,
                                                   AlbumPhotoSequence& sequence) {
    if (album.empty() || album.size() > 96 || album.find('/') != std::string::npos ||
        !ValidUtf8(album))
        return {Code::Invalid, "Tên album không hợp lệ."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;

    sequence = {};
    sequence.truncated = photo_index_truncated_;
    const std::string prefix = std::string("/robot/images/") + album + "/";
    for (size_t i = 0; i < photo_count_; ++i) {
        if (std::strncmp(photo_index_[i].path, prefix.c_str(), prefix.size()) != 0)
            continue;
        sequence.indexes[sequence.count++] = static_cast<uint16_t>(i);
    }
    std::sort(sequence.indexes.begin(), sequence.indexes.begin() + sequence.count,
              [this](uint16_t left, uint16_t right) {
                  return std::strcmp(photo_index_[left].path, photo_index_[right].path) < 0;
              });
    if (sequence.count == 0)
        return {Code::NotFound, "Không tìm thấy ảnh trong album."};
    return {};
}

SdStorage::Result SdStorage::GetPhotoByIndex(uint16_t index, Photo& photo) {
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = BuildPhotoIndex(); !result)
        return result;
    if (index >= photo_count_)
        return {Code::NotFound, "Ảnh trong album đã thay đổi."};

    const std::string path(photo_index_[index].path);
    const size_t slash = path.find_last_of('/');
    const size_t extension = path.find_last_of('.');
    photo.path = path;
    photo.name = path.substr(slash + 1, extension - slash - 1);
    constexpr size_t kPhotoRootLength = sizeof("/robot/images") - 1;
    const size_t album_slash = path.find('/', kPhotoRootLength + 1);
    if (album_slash != std::string::npos)
        photo.album = path.substr(kPhotoRootLength + 1, album_slash - kPhotoRootLength - 1);
    else
        photo.album.clear();
    photo.size = photo_index_[index].size;
    return {};
}

SdStorage::Result SdStorage::ReadPhotoJpeg(const std::string& path, uint8_t*& data, size_t& size,
                                           size_t& width, size_t& height) {
    constexpr char kPhotoRoot[] = "/robot/images/";
    if (path.rfind(kPhotoRoot, 0) != 0)
        return {Code::Invalid, "Ảnh MCP chỉ được đọc trong /robot/images/."};
    return ReadJpeg(path, data, size, width, height);
}

SdStorage::Result SdStorage::MakeDirectory(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (mkdir(full.c_str(), 0775) == 0) {
        InvalidateSpace();
        InvalidatePhotoIndex();
        return {};
    }
    if (errno == EEXIST)
        return {Code::Exists, "Tên này đã tồn tại."};
    return IoError("mkdir");
}

SdStorage::Result SdStorage::Remove(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (!upload_path_.empty())
        return {Code::Busy, "Hãy hoàn tất hoặc hủy upload trước."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return {Code::NotFound, "Không tìm thấy file/thư mục."};
    const bool is_directory = S_ISDIR(info.st_mode);
    const bool is_music_track =
        !is_directory && path.compare(0, sizeof(kMusicRoot) - 1, kMusicRoot) == 0 &&
        path.size() > sizeof(kMusicRoot) && path[sizeof(kMusicRoot) - 1] == '/' &&
        path.find('/', sizeof(kMusicRoot)) == std::string::npos && HasAudioExtension(path);
    if (path == "/robot" || path == kMusicRoot ||
        path.compare(0, sizeof(kMusicRoot), std::string(kMusicRoot) + "/") == 0)
        InvalidateMusicIndex();
    if (is_directory) {
        if (auto result = RemoveTree(full, 0); !result)
            return result;
    }
    int rc = is_directory ? rmdir(full.c_str()) : unlink(full.c_str());
    if (rc == 0) {
        if (is_music_track) {
            if (auto result = WriteMusicMetadataOverrides(path, "", "", "", true); !result)
                ESP_LOGW(kTag, "Could not remove metadata override for deleted track: %s",
                         path.c_str());
        }
        InvalidateSpace();
        InvalidatePhotoIndex();
        return {};
    }
    if (is_directory && (errno == ENOTEMPTY || errno == EEXIST))
        return {Code::Invalid,
                "Thư mục còn mục chưa xóa được hoặc đã thay đổi trong lúc thao tác."};
    return IoError("remove");
}

SdStorage::Result SdStorage::RemoveTree(const std::string& full_path, unsigned depth) {
    if (depth >= kMaxDeleteDepth)
        return {Code::Limit, "Thư mục lồng quá sâu; dừng để giới hạn xóa an toàn."};

    DIR* raw = opendir(full_path.c_str());
    if (!raw)
        return IoError("opendir recursive delete");
    std::unique_ptr<DIR, decltype(&closedir)> dir(raw, closedir);
    size_t visited = 0;
    while (true) {
        errno = 0;
        dirent* item = readdir(dir.get());
        if (!item) {
            if (errno != 0)
                return IoError("readdir recursive delete");
            break;
        }
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;

        const std::string child = full_path + "/" + item->d_name;
        struct stat info = {};
        if (stat(child.c_str(), &info) != 0)
            return IoError("stat recursive delete");

        if (S_ISDIR(info.st_mode)) {
            // Keep only one FATFS directory open while descending.
            dir.reset();
            if (auto result = RemoveTree(child, depth + 1); !result)
                return result;
            if (rmdir(child.c_str()) != 0)
                return IoError("rmdir recursive delete");
            raw = opendir(full_path.c_str());
            if (!raw)
                return IoError("reopen parent after recursive delete");
            dir.reset(raw);
        } else if (unlink(child.c_str()) != 0) {
            return IoError("unlink recursive delete");
        }

        if (++visited % 16 == 0)
            vTaskDelay(1);
    }
    return {};
}

SdStorage::Result SdStorage::Upload(const std::string& path, uint64_t offset, uint64_t total,
                                    const char* chunk, size_t chunk_size) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    if (total > kMaxFileBytes || chunk_size > kUploadChunkBytes || offset > total ||
        chunk_size > total - offset || (chunk_size == 0 && total != 0) ||
        (chunk_size != 0 && chunk == nullptr))
        return {Code::Limit, "File tối đa 1 GiB, mỗi khối tối đa 1 MiB."};
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (!upload_path_.empty() && upload_path_ != path)
        return {Code::Busy, "Đang upload file khác. Hãy hủy hoặc hoàn tất trước."};
    struct stat info = {};
    if (stat(full.c_str(), &info) == 0)
        return {Code::Exists, "File đã tồn tại. Đổi tên hoặc xóa file cũ trước."};
    if (errno != ENOENT)
        return IoError("stat upload target");
    const std::string temporary = full + kTemporarySuffix;
    if (offset == 0) {
        upload_path_ = path;
        upload_total_ = total;
        upload_offset_ = 0;
    } else if (upload_path_ != path || upload_total_ != total || upload_offset_ != offset) {
        return {Code::Invalid, "Vị trí upload không khớp. Hãy tải lại từ đầu."};
    }
    if (offset > 0 && (stat(temporary.c_str(), &info) != 0 || info.st_size < 0 ||
                       static_cast<uint64_t>(info.st_size) != offset))
        return {Code::Invalid, "File tạm không khớp offset. Hủy upload rồi tải lại từ đầu."};
    std::unique_ptr<FILE, decltype(&fclose)> file(
        fopen(temporary.c_str(), offset == 0 ? "wb" : "ab"), fclose);
    if (!file)
        return IoError("open upload");
    if ((chunk_size != 0 && fwrite(chunk, 1, chunk_size, file.get()) != chunk_size) ||
        fflush(file.get()) != 0)
        return IoError("write upload");
    // Flush every HTTP chunk, but only force a card sync before committing the
    // completed temporary file. This avoids a slow sync after each small request.
    if (upload_offset_ + chunk_size == total && fsync(fileno(file.get())) != 0)
        return IoError("sync upload");
    FILE* completed = file.release();
    if (fclose(completed) != 0)
        return IoError("close upload");
    upload_offset_ += chunk_size;
    InvalidateSpace();
    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        status_.upload_path = path;
        status_.uploaded_bytes = upload_offset_;
    }
    if (upload_offset_ == total) {
        if (rename(temporary.c_str(), full.c_str()) != 0)
            return IoError("commit upload");
        InvalidatePhotoIndex();
        ClearUpload();
        const size_t music_root_length = sizeof(kMusicRoot) - 1;
        if (path.compare(0, music_root_length, kMusicRoot) == 0 &&
            path.size() > music_root_length + 1 && path[music_root_length] == '/' &&
            path.find('/', music_root_length + 1) == std::string::npos && HasAudioExtension(path))
            InvalidateMusicIndex();
    }
    return {};
}

SdStorage::Result SdStorage::AbortUpload(const std::string& path) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (!card_)
        return {Code::Unavailable, "Thẻ chưa sẵn sàng."};
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    if (std::remove((full + kTemporarySuffix).c_str()) != 0 && errno != ENOENT)
        return IoError("abort upload");
    if (path == upload_path_)
        ClearUpload();
    InvalidateSpace();
    if (music_dirty_ && worker_)
        xTaskNotify(worker_, 0, eNoAction);
    return {};
}

SdStorage::Result SdStorage::Download(const std::string& path,
                                      const std::function<bool(const char*, size_t)>& sink) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;
    if (playback_files_.load() != 0)
        return {Code::Busy, "Đang phát file từ thẻ. Dừng phát rồi thử lại."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return {Code::NotFound, "Không tìm thấy file."};
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<uint64_t>(info.st_size) > kMaxFileBytes)
        return {Code::Limit, "Chỉ tải xuống file thường tối đa 1 GiB."};
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full.c_str(), "rb"), fclose);
    if (!file)
        return IoError("open download");
    auto buffer = std::make_unique<std::array<char, kChunkBytes>>();
    while (true) {
        size_t count = fread(buffer->data(), 1, buffer->size(), file.get());
        if (count && !sink(buffer->data(), count))
            return {Code::Io, "Kết nối tải xuống đã đóng."};
        if (count < buffer->size()) {
            if (ferror(file.get()))
                return IoError("read download");
            break;
        }
        vTaskDelay(1);
    }
    return {};
}

SdStorage::Result SdStorage::ReadJpeg(const std::string& path, uint8_t*& data, size_t& size,
                                      size_t& width, size_t& height) {
    data = nullptr;
    size = width = height = 0;
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;

    const size_t basename = path.find_last_of('/') + 1;
    const size_t extension_pos = path.find_last_of('.');
    std::string extension = extension_pos == std::string::npos || extension_pos < basename
                                ? ""
                                : path.substr(extension_pos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".jpg" && extension != ".jpeg")
        return {Code::Invalid, "Chỉ hỗ trợ ảnh JPEG (.jpg, .jpeg)."};

    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;

    constexpr size_t kMaxJpegBytes = 512 * 1024;
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0)
        return errno == ENOENT ? Result{Code::NotFound, "Không tìm thấy ảnh."}
                               : IoError("stat JPEG");
    if (!S_ISREG(info.st_mode) || info.st_size < 4 ||
        static_cast<uint64_t>(info.st_size) > kMaxJpegBytes)
        return {Code::Limit, "Ảnh JPEG phải có dung lượng tối đa 512 KiB."};

    const auto length = static_cast<size_t>(info.st_size);
    auto* buffer =
        static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer)
        return {Code::Unavailable, "Không đủ PSRAM để nạp ảnh JPEG."};
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> owner(buffer, heap_caps_free);
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(full.c_str(), "rb"), fclose);
    if (!file)
        return IoError("open JPEG");
    if (fread(buffer, 1, length, file.get()) != length || ferror(file.get()))
        return IoError("read JPEG");

    // Validate dimensions before decoding so malformed images cannot request
    // an unbounded RGB565 frame buffer from the JPEG decoder.
    if (buffer[0] != 0xff || buffer[1] != 0xd8)
        return {Code::Invalid, "File không phải ảnh JPEG hợp lệ."};
    size_t position = 2;
    bool found_dimensions = false;
    while (position + 4 <= length) {
        while (position < length && buffer[position] != 0xff)
            ++position;
        while (position < length && buffer[position] == 0xff)
            ++position;
        if (position >= length)
            break;
        const uint8_t marker = buffer[position++];
        if (marker == 0xd9 || marker == 0xda)
            break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (position + 2 > length)
            break;
        const size_t segment_length = (static_cast<size_t>(buffer[position]) << 8) |
                                      static_cast<size_t>(buffer[position + 1]);
        if (segment_length < 2 || segment_length > length - position)
            break;
        const bool is_sof =
            (marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf);
        if (is_sof) {
            if (segment_length < 7)
                break;
            height = (static_cast<size_t>(buffer[position + 3]) << 8) | buffer[position + 4];
            width = (static_cast<size_t>(buffer[position + 5]) << 8) | buffer[position + 6];
            found_dimensions = width != 0 && height != 0 && width <= 512 && height <= 512;
            break;
        }
        position += segment_length;
    }
    if (!found_dimensions)
        return {Code::Limit, "JPEG không hợp lệ hoặc vượt giới hạn 512 × 512 pixel."};

    size = length;
    data = owner.release();
    return {};
}

void SdStorage::GetMusicMetadata(const std::string& path, MediaPlaybackMetadata& metadata) const {
    metadata.source_path = path.compare(0, sizeof(kMusicRoot) - 1, kMusicRoot) == 0 &&
                                   path.size() > sizeof(kMusicRoot) - 1 &&
                                   path[sizeof(kMusicRoot) - 1] == '/'
                               ? path
                               : "";
    std::lock_guard<std::mutex> lock(music_mutex_);
    for (size_t i = 0; i < music_count_; ++i) {
        if (path == music_index_[i].path) {
            metadata.title = music_index_[i].title;
            metadata.artist = music_index_[i].artist;
            metadata.album = music_index_[i].album;
            metadata.duration_ms = music_index_[i].duration_ms;
            return;
        }
    }
    metadata.title = path.substr(path.find_last_of('/') + 1);
    metadata.duration_ms = 0;
}

SdStorage::Result SdStorage::CreateAudioInput(const std::string& path,
                                              NotifyPlayer::InputFactory& factory) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::unique_lock<std::mutex> lock(io_mutex_, std::try_to_lock);
    if (!lock)
        return {Code::Busy, "Thẻ đang bận."};
    if (auto result = Ready(); !result)
        return result;

    const size_t basename = path.find_last_of('/') + 1;
    const size_t extension_pos = path.find_last_of('.');
    std::string extension = extension_pos == std::string::npos || extension_pos < basename
                                ? ""
                                : path.substr(extension_pos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".mp3" && extension != ".ogg" && extension != ".opus")
        return {Code::Invalid, "Hỗ trợ phát MP3 và Ogg/Opus (.mp3, .ogg, .opus)."};
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
        return {Code::Invalid, "File âm thanh không tồn tại, rỗng hoặc không hợp lệ."};

    factory = [this, path]() { return std::make_unique<SdAudioInput>(*this, path); };
    return {};
}

SdStorage::Result SdStorage::OpenAudioFile(const std::string& path, FILE*& file) {
    std::string full;
    if (auto result = Resolve(path, full, false); !result)
        return result;
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (auto result = Ready(); !result)
        return result;
    struct stat info = {};
    if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
        return {Code::NotFound, "Không tìm thấy file âm thanh hợp lệ."};
    file = fopen(full.c_str(), "rb");
    if (!file)
        return IoError("open local audio");
    playback_files_.fetch_add(1);
    return {};
}

bool SdStorage::ReadAudioFile(FILE* file, char* buffer, size_t capacity, size_t& bytes_read) {
    if (!file || !buffer || capacity == 0)
        return false;
    std::lock_guard<std::mutex> lock(io_mutex_);
    bytes_read = fread(buffer, 1, capacity, file);
    return bytes_read != 0 || !ferror(file);
}

void SdStorage::CloseAudioFile(FILE* file) {
    if (!file)
        return;
    std::lock_guard<std::mutex> lock(io_mutex_);
    fclose(file);
    playback_files_.fetch_sub(1);
    if (music_dirty_ && worker_)
        xTaskNotify(worker_, 0, eNoAction);
}
}  // namespace kynangso
