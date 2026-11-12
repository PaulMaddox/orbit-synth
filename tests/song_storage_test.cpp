#include "../Orbit/song_storage.h"
#include "../Orbit/ui_layout.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace orbit;
namespace {
struct MemoryFile {
    std::vector<uint8_t> *data = nullptr;
    size_t position = 0;
    size_t limit = std::numeric_limits<size_t>::max();
    explicit operator bool() const { return data != nullptr; }
    size_t size() const { return data ? data->size() : 0; }
    size_t read(uint8_t *out, size_t count) {
        count = std::min({count, size() - position, limit > position ? limit - position : 0});
        if (count) std::memcpy(out, data->data() + position, count);
        position += count;
        return count;
    }
    size_t write(const uint8_t *in, size_t count) {
        count = std::min(count, limit > position ? limit - position : 0);
        data->insert(data->end(), in, in + count);
        position += count;
        return count;
    }
    void flush() {}
    void close() {}
};

struct MemoryFS {
    std::map<std::string, std::vector<uint8_t>> files;
    size_t limit = std::numeric_limits<size_t>::max();
    bool failRename = false;
    MemoryFile open(const char *path, const char *mode) {
        if (*mode == 'w') files[path].clear();
        auto it = files.find(path);
        return {it == files.end() ? nullptr : &it->second, 0, limit};
    }
    bool rename(const char *from, const char *to) {
        if (failRename) return false;
        files[to] = files.at(from);
        files.erase(from);
        return true;
    }
};

void settle(Engine &engine) {
    engine.stop();
    int16_t block[64];
    for (int i = 0; i < SampleRate * 2 / 64; ++i) engine.render(block, 64);
    assert(engine.idle());
}

bool load(MemoryFS &fs, const char *path, Engine &engine) {
    Session song;
    std::vector<int16_t> recording(SampleCapacity);
    auto file = fs.open(path, "r");
    return readSong(file, song, recording.data()) &&
        engine.restoreSong(song, recording.data()) == Result::Ok;
}

std::vector<uint8_t> legacySong(const Session &song, const int16_t *sample, int version) {
    Session old = song;
    old.version = version;
    for (Track &track : old.tracks) track.reserved = version == 4 ? 1 : 0x7f;
    for (Scene &scene : old.scenes)
        for (Track &track : scene.tracks) track.reserved = version == 4 ? 1 : 0x7f;
    std::vector<uint8_t> bytes;
    auto append = [&](const void *data, size_t size) {
        const auto *start = static_cast<const uint8_t *>(data);
        bytes.insert(bytes.end(), start, start + size);
    };
    append(&old, offsetof(Session, tracks));
    for (const Track &track : old.tracks)
        append(&track, version == 1 ? offsetof(Track, pitch) : sizeof(Track));
    if (version >= 3)
        append(reinterpret_cast<const uint8_t *>(&old) + LegacySessionBytes,
            sizeof(Session) - LegacySessionBytes);
    append(sample, song.sampleCount * sizeof(int16_t));
    uint32_t hash = songChecksum(bytes.data(), bytes.size());
    append(&hash, sizeof(hash));
    return bytes;
}
}

int main() {
    auto source = std::make_unique<Engine>();
    auto current = std::make_unique<Engine>();
    assert(source->loadDemo(2) == Result::Ok);
    source->session.sampleCount = SampleCapacity;
    source->session.volume = 47;
    source->session.slices = true;
    source->session.fx.enabled = true;
    source->session.fx.points[7] = {23, 81, true};
    source->session.scenes[1].slices = true;
    source->session.tracks[0].pitch = -7;
    for (int i = 0; i < SampleCapacity; ++i) source->sample[i] = int16_t(i * 17);
    MemoryFS fs;
    assert(writeSong(fs, "/song1.bin", "/song.tmp", source->session, source->sample));
    const auto first = fs.files.at("/song1.bin");
    assert(load(fs, "/song1.bin", *current));
    assert(std::memcmp(&source->session, &current->session, sizeof(Session)) == 0);
    assert(std::memcmp(source->sample, current->sample, SampleCapacity * sizeof(int16_t)) == 0);
    assert(!current->status().playing && !current->status().undo && !current->status().fxUndo);

    // Independent slots survive failed writes, failed replacement and a simulated restart.
    assert(source->loadDemo(5) == Result::Ok);
    assert(writeSong(fs, "/song8.bin", "/song.tmp", source->session, source->sample));
    assert(fs.files.at("/song1.bin") == first);
    fs.limit = sizeof(Session) + 10;
    assert(!writeSong(fs, "/song1.bin", "/song.tmp", source->session, source->sample));
    assert(fs.files.at("/song1.bin") == first);
    fs.limit = std::numeric_limits<size_t>::max();
    fs.failRename = true;
    assert(!writeSong(fs, "/song1.bin", "/song.tmp", source->session, source->sample));
    assert(fs.files.at("/song1.bin") == first);
    fs.failRename = false;
    auto restarted = std::make_unique<Engine>();
    assert(load(fs, "/song8.bin", *restarted));
    assert(std::memcmp(&source->session, &restarted->session, sizeof(Session)) == 0);
    assert(writeSong(fs, "/song1.bin", "/song.tmp", source->session, source->sample));
    assert(load(fs, "/song1.bin", *current));
    assert(std::memcmp(&source->session, &current->session, sizeof(Session)) == 0);

    // Bad data and incomplete reads never change the live song or its recording.
    Session before = current->session;
    std::vector<int16_t> beforeSample(current->sample, current->sample + SampleCapacity);
    assert(!load(fs, "/missing.bin", *current));
    const auto good = fs.files.at("/song1.bin");
    fs.files["/song1.bin"][sizeof(Session) + 3] ^= 0x40;
    assert(!load(fs, "/song1.bin", *current));
    fs.files["/song1.bin"] = good;
    fs.files["/song1.bin"].pop_back();
    assert(!load(fs, "/song1.bin", *current));
    fs.files["/song1.bin"] = good;
    fs.limit = sizeof(Session) + 17;
    assert(!load(fs, "/song1.bin", *current));
    fs.limit = std::numeric_limits<size_t>::max();
    fs.files["/song1.bin"][0] ^= 1;
    assert(!load(fs, "/song1.bin", *current));
    assert(std::memcmp(&before, &current->session, sizeof(Session)) == 0);
    assert(std::equal(beforeSample.begin(), beforeSample.end(), current->sample));

    for (int version = 1; version <= 4; ++version) {
        fs.files["/orbit.bin"] = legacySong(source->session, source->sample, version);
        assert(load(fs, "/orbit.bin", *current));
        assert(Engine::valid(current->session));
        assert(current->session.bpm == source->session.bpm);
        assert(std::memcmp(current->sample, source->sample,
            source->session.sampleCount * sizeof(int16_t)) == 0);
    }

    // Switching songs discards old undo/FX state, refuses live performance and accepts empty audio.
    settle(*current);
    current->clearSelected();
    assert(current->status().undo);
    current->touchFx(20, 90);
    assert(current->restoreSong(source->session, source->sample) == Result::Busy);
    current->releaseFx();
    current->togglePlay();
    assert(current->restoreSong(source->session, source->sample) == Result::Busy);
    settle(*current);
    assert(current->restoreSong(source->session, source->sample) == Result::Ok);
    assert(!current->status().undo && !current->status().fxTouched && current->status().nextScene == -1);
    source->session.sampleCount = 0;
    assert(writeSong(fs, "/empty.bin", "/song.tmp", source->session, nullptr));
    assert(load(fs, "/empty.bin", *current));
    assert(current->session.sampleCount == 0);
    Session invalid = source->session;
    invalid.bpm = 0;
    assert(current->restoreSong(invalid, nullptr) == Result::Invalid);
    for (const auto &card : ui::SongCards) {
        assert(ui::hit(ui::SongCards, card.x, card.y) >= 0);
        assert(card.y + card.h < ui::FooterTouch[0].y);
    }
    assert(!ui::SongSave.contains(ui::SongLoad.x, ui::SongLoad.y));
    std::puts("Song storage tests passed");
}
