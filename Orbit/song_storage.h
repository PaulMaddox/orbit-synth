#pragma once

#include "engine.h"

namespace orbit {
constexpr int SongSlots = 8;

inline uint32_t songChecksum(const uint8_t *data, size_t size, uint32_t hash = 2166136261u) {
    for (size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}

// Read into staging memory; callers must not replace the live song until this succeeds.
template <typename File>
bool readSong(File &file, Session &loaded, int16_t *sample) {
    uint32_t storedHash = 0;
    constexpr size_t headerBytes = offsetof(Session, tracks);
    if (!file || file.read(reinterpret_cast<uint8_t *>(&loaded), headerBytes) != headerBytes ||
        loaded.version < 1 || loaded.version > 5 || loaded.sampleCount > SampleCapacity) return false;
    uint16_t storedVersion = loaded.version;
    size_t trackBytes = storedVersion == 1 ? offsetof(Track, pitch) : sizeof(Track);
    size_t extensionBytes = storedVersion >= 3 ? sizeof(Session) - LegacySessionBytes : 0;
    size_t bytes = loaded.sampleCount * sizeof(int16_t);
    if (file.size() != headerBytes + TrackCount * trackBytes + extensionBytes +
        bytes + sizeof(storedHash)) return false;
    uint32_t hash = songChecksum(reinterpret_cast<const uint8_t *>(&loaded), headerBytes);
    for (Track &track : loaded.tracks) {
        if (file.read(reinterpret_cast<uint8_t *>(&track), trackBytes) != trackBytes) return false;
        hash = songChecksum(reinterpret_cast<const uint8_t *>(&track), trackBytes, hash);
    }
    if (extensionBytes) {
        auto *extension = reinterpret_cast<uint8_t *>(&loaded) + LegacySessionBytes;
        if (file.read(extension, extensionBytes) != extensionBytes) return false;
        hash = songChecksum(extension, extensionBytes, hash);
    }
    Engine::upgradeSession(loaded, storedVersion);
    if (!Engine::valid(loaded)) return false;
    if (file.read(reinterpret_cast<uint8_t *>(sample), bytes) != bytes ||
        file.read(reinterpret_cast<uint8_t *>(&storedHash), sizeof(storedHash)) != sizeof(storedHash))
        return false;
    return songChecksum(reinterpret_cast<const uint8_t *>(sample), bytes, hash) == storedHash;
}

template <typename FileSystem>
bool writeSong(FileSystem &fs, const char *path, const char *temporary,
               const Session &song, const int16_t *sample) {
    if (!Engine::valid(song)) return false;
    auto file = fs.open(temporary, "w");
    size_t bytes = song.sampleCount * sizeof(int16_t);
    uint32_t hash = songChecksum(reinterpret_cast<const uint8_t *>(&song), sizeof(song));
    hash = songChecksum(reinterpret_cast<const uint8_t *>(sample), bytes, hash);
    bool ok = file &&
        file.write(reinterpret_cast<const uint8_t *>(&song), sizeof(song)) == sizeof(song) &&
        file.write(reinterpret_cast<const uint8_t *>(sample), bytes) == bytes &&
        file.write(reinterpret_cast<const uint8_t *>(&hash), sizeof(hash)) == sizeof(hash);
    file.flush();
    ok = ok && file.size() == sizeof(song) + bytes + sizeof(hash);
    file.close();
    // LittleFS replaces the destination atomically; never remove the previous song first.
    return ok && fs.rename(temporary, path);
}
}
