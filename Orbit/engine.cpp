#include "engine.h"
#include "demo_sample.h"
#ifndef ORBIT_CALIBRATE
#include "sound_levels.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orbit {
namespace {
constexpr float Pi = 3.14159265358979323846f;
// Accumulating BPM per sample makes the hit follow tempo changes mid-note.
constexpr uint32_t HitDuration = SampleRate * 30;
constexpr uint32_t HitFade = HitDuration / 16;
constexpr int scales[3][8] = {
    {0, 3, 5, 7, 10, 12, 15, 17},
    {0, 2, 4, 7, 9, 12, 14, 16},
    {0, 2, 3, 5, 7, 8, 10, 12}
};
struct SpeakerBody { uint8_t first, second, gain; };
constexpr SpeakerBody SpeakerBodies[SoundCount] = {
    {5,9,96}, {3,7,80}, {4,9,64}, {1,1,0}, {5,9,64}, {3,8,96},
    {5,10,80}, {5,8,80}, {6,10,96}, {7,11,64}, {5,9,96}, {4,9,128},
    {7,11,96}, {5,11,64}, {3,5,48}, {1,1,0}, {4,9,64}, {1,1,0},
    {1,1,0}, {4,7,96}, {3,7,96}, {5,9,128}, {4,9,96}, {1,1,0},
    {7,13,64}, {11,17,128}, {1,1,0}, {5,9,48}, {5,11,96}, {1,1,0}
};
template <typename T> T clamp(T value, T low, T high) {
    return std::max(low, std::min(value, high));
}
int32_t bandlimitedSaw(uint32_t phase, uint32_t increment) {
    int32_t position = phase >> 16;
    int32_t width = std::max<uint32_t>(1, increment >> 16);
    int32_t correction = 0;
    if (position < width) {
        int32_t x = int64_t(position) * 65536 / width;
        correction = 2 * x - (int64_t(x) * x >> 16) - 65536;
    } else if (position > 65536 - width) {
        int32_t x = int64_t(position - 65536) * 65536 / width;
        correction = (int64_t(x) * x >> 16) + 2 * x + 65536;
    }
    return position - 32768 - correction / 2;
}
}

const DemoInfo DemoLibrary[DemoCount] = {
    {"SUNLIT", "ACOUSTIC POP", 100},
    {"VELVET", "LATE-NIGHT JAZZ", 112},
    {"STARDUST", "CINEMATIC", 86},
    {"TUBA PARADE", "BRASS BAND", 116},
    {"NEON", "EUPHORIC CLUB", 126},
    {"IRON", "HEAVY GUITAR", 138},
    {"OPEN ROAD", "ACOUSTIC POP", 108},
    {"SEASIDE", "ACOUSTIC POP", 98},
    {"FIREFLY", "ACOUSTIC POP", 92},
    {"DISCO GLOW", "SUNSHINE DISCO", 116},
    {"NEON HEART", "80S SYNTH-FUNK", 120},
    {"SATURDAY", "RETRO DANCE-POP", 118},
};
const char *const SceneNames[SceneCount] = {"INTRO", "GROOVE", "BREAK", "DROP"};

const char *const SoundFamilies[SoundFamilyCount] = {
    "GUITARS", "KEYS", "STRINGS", "WINDS", "BRASS", "BASS", "ELECTRO", "PADS"
};

const SoundPreset Sounds[SoundCount] = {
    {"ACOUSTIC GUITAR", 0, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"WARM PAD",    7, SoundWave::Warm,   1.006, 1600, 0, 10, 100, 128, 0, false},
    {"GLOCK",       1, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"ACID",        6, SoundWave::Acid,   1.006,   64,  0,  8, 100, 128,  0, true},
    {"METAL GUITAR",0, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"HARP",        2, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"MARIMBA",     1, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"PIANO",       1, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"CLEAN GUITAR",0, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"ORGAN",       1, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"SAXOPHONE",   3, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"TRUMPET",     4, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"FRENCH HORN", 4, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"TROMBONE",    4, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"CELLO",       2, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"STRINGS",     2, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"DREAM PAD",   7, SoundWave::Pad,    2.003, 2000,  0, 10, 110, 160,  1, false},
    {"SAW STACK",   6, SoundWave::Saw,    1.009,   64,  0,  8, 125, 132,  0, false},
    {"PULSE LEAD",  6, SoundWave::Square, 1.000,   32,  0,  8, 100, 116,  0, false},
    {"FLUTE",       3, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
    {"BOWED BASS",  5, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"DEEP BASS",   5, SoundWave::Sub,    0.500,   64,  0,  8,  85, 164,  0, false},
    {"PLUCK BASS",  5, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"GROWL BASS",  5, SoundWave::Growl,  1.003,   64,  0,  8, 115, 140,  0, true},
    {"CLARINET",    3, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"OBOE",        3, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"GAME BASS",   6, SoundWave::Chip,   1.001,   32,  0,  8, 105, 128,  1, false},
    {"VOX PAD",     7, SoundWave::Choir,  1.007, 1600,  0, 10, 100, 154,  0, false},
    {"TUBA",        4, SoundWave::Sample, 1, 16, 0, 8, 100, 128, 0, false},
    {"SOLO VIOLIN", 2, SoundWave::Sample, 1, 16, 0, 9, 100, 128, 0, false},
};

int familySound(int family, int position) {
    if (family < 0 || family >= SoundFamilyCount || position < 0) return -1;
    for (int i = 0; i < SoundCount; ++i)
        if (Sounds[i].family == family && position-- == 0) return i;
    return -1;
}

Engine::Engine() {
    for (int i = 0; i < 1024; ++i) sine_[i] = std::sin(i * 2 * Pi / 1024) * 32767;
    session.tracks[1].patch = 22;
    session.tracks[1].tone = 35;
    session.tracks[1].space = 10;
    session.tracks[2].space = 8;
    initializeScenes(session);
}

int Engine::midiNote(int track, int pad, int root, int scale) {
    if (track < 0 || track >= TrackCount || pad < 0 || pad >= 8 ||
        root < 0 || root > 11 || scale < 0 || scale > 2) return -1;
    return (track == 1 ? 48 : 60) + root + scales[scale][pad];
}

bool Engine::valid(const Session &s) {
    if (s.magic != 0x4f524254 || s.version != 5 || s.bpm < 60 || s.bpm > 180 ||
        s.volume > 80 || s.root > 11 || s.scale > 2 || s.swing > 40 ||
        s.sampleCount > SampleCapacity) return false;
    auto validTracks = [](const Track *tracks) {
        for (int track = 0; track < TrackCount; ++track) {
            const Track &t = tracks[track];
            if (t.count > MaxNotes || t.patch >= SoundCount || t.tone > 100 || t.space > 100 ||
                t.pitch < -12 || t.pitch > 12 || t.reserved != 0) return false;
            for (int i = 0; i < t.count; ++i) {
                const Note &n = t.notes[i];
                if (n.tick >= LoopTicks || n.duration == 0 || n.duration > LoopTicks ||
                    (n.pad > 7 && !(track == 3 && n.pad >= 128 && n.pad <= 135)) ||
                    n.velocity > 127) return false;
            }
        }
        return true;
    };
    auto validFx = [](const FxLoop &fx) {
        for (const FxPoint &point : fx.points)
            if (point.x > 100 || point.y > 100) return false;
        return true;
    };
    if (!validTracks(s.tracks) || !validFx(s.fx) || s.activeScene >= SceneCount) return false;
    for (const Scene &scene : s.scenes)
        if (!validTracks(scene.tracks) || !validFx(scene.fx)) return false;
    return true;
}

void Engine::initializeScenes(Session &s) {
    s.fx = FxLoop{};
    s.slices = false;
    s.activeScene = 3;
    const uint8_t masks[] = {4, 6, 9, 15};
    for (int i = 0; i < SceneCount; ++i) {
        Scene &scene = s.scenes[i];
        scene.fx = FxLoop{};
        scene.slices = false;
        for (int track = 0; track < TrackCount; ++track) {
            scene.tracks[track] = s.tracks[track];
            if (!(masks[i] & (1 << track))) scene.tracks[track].muted = true;
        }
    }
}

void Engine::upgradeSession(Session &s, uint16_t storedVersion) {
    if (storedVersion < 4) {
        for (int track = 0; track < TrackCount; ++track) {
            if (storedVersion == 1) s.tracks[track].pitch = 0;
            s.tracks[track].reserved = 0;
        }
        if (storedVersion < 3) initializeScenes(s);
        else for (Scene &scene : s.scenes)
            for (int track = 0; track < TrackCount; ++track)
                scene.tracks[track].reserved = 0;
    }
    if (storedVersion == 4) {
        // Version 4 used the reserved byte for auto/whole/hold (0/1/2).
        auto upgradeTracks = [](Track *tracks) {
            for (int track = 0; track < TrackCount; ++track) {
                Track &t = tracks[track];
                if (t.reserved > 2) continue;
                bool whole = t.reserved == 1 || (t.reserved == 0 &&
                    t.patch < SoundCount && Sounds[t.patch].wave == SoundWave::Sample);
                if (track < 2 && whole)
                    for (int i = 0; i < std::min<int>(t.count, MaxNotes); ++i)
                        t.notes[i].duration = 2 * TicksPerStep;
                t.reserved = 0;
            }
        };
        upgradeTracks(s.tracks);
        for (Scene &scene : s.scenes) upgradeTracks(scene.tracks);
    }
    s.version = 5;
}

void Engine::storeScene(int scene) {
    Scene &target = session.scenes[scene];
    std::copy_n(session.tracks, TrackCount, target.tracks);
    target.fx = session.fx;
    target.slices = session.slices;
}

void Engine::applyScene(int scene) {
    noteOff();
    storeScene(session.activeScene);
    const Scene &source = session.scenes[scene];
    std::copy_n(source.tracks, TrackCount, session.tracks);
    session.fx = source.fx;
    session.slices = source.slices;
    session.activeScene = scene;
    nextScene_ = -1;
    for (Voice &voice : voices_) voice.released = true;
    undoValid_ = fxUndoValid_ = false;
    ++revision_;
}

Result Engine::queueScene(int scene) {
    if (scene < 0 || scene >= SceneCount) return Result::Invalid;
    if (capturing_ || recording_ || armed_ || fxRecording_ || fxArmed_) return Result::Busy;
    if (scene == session.activeScene || scene == nextScene_) {
        nextScene_ = -1;
        return Result::Ok;
    }
    if (playing_) nextScene_ = scene;
    else applyScene(scene);
    return Result::Ok;
}

Result Engine::copyScene(int destination) {
    if (destination < 0 || destination >= SceneCount) return Result::Invalid;
    if (capturing_ || recording_ || armed_ || fxRecording_ || fxArmed_) return Result::Busy;
    storeScene(destination);
    ++revision_;
    return Result::Ok;
}

Result Engine::setSlices(bool enabled) {
    if (capturing_) return Result::Busy;
    if (enabled && session.sampleCount < 16) return Result::NoSample;
    if (session.slices != enabled) { noteOff(); session.slices = enabled; ++revision_; }
    return Result::Ok;
}

FxPoint Engine::effectPoint() const {
    if (fxTouched_) return touchEffect_;
    if (playing_ && session.fx.enabled && preparation_ != RecordPreparation::GetReady && !fxRecording_)
        return session.fx.points[tick_ * FxSteps / LoopTicks];
    return FxPoint{};
}

void Engine::updateFxRecording() {
    if (!fxRecording_) return;
    session.fx.points[tick_ * FxSteps / LoopTicks] =
        fxTouched_ ? touchEffect_ : FxPoint{};
    ++revision_;
}

Result Engine::touchFx(int x, int y) {
    if (x < 0 || x > 100 || y < 0 || y > 100) return Result::Invalid;
    if (capturing_) return Result::Busy;
    touchEffect_ = FxPoint{uint8_t(x), uint8_t(y), true};
    fxTouched_ = true;
    updateFxRecording();
    return Result::Ok;
}

void Engine::releaseFx() {
    fxTouched_ = false;
    updateFxRecording();
}

Result Engine::recordFx() {
    if (capturing_ || recording_ || armed_ || nextScene_ >= 0) return Result::Busy;
    if (fxArmed_) {
        fxArmed_ = false;
        cancelPreparation();
    } else if (fxRecording_) {
        finishFxRecording();
    } else {
        fxArmed_ = true;
        beginPreparation();
    }
    return Result::Ok;
}

void Engine::finishFxRecording() {
    fxRecording_ = false;
    session.fx.enabled = std::any_of(session.fx.points, session.fx.points + FxSteps,
        [](const FxPoint &point) { return point.active; });
    ++revision_;
}

Result Engine::enableFx(bool enabled) {
    if (fxRecording_ || fxArmed_ || capturing_) return Result::Busy;
    session.fx.enabled = enabled;
    ++revision_;
    return Result::Ok;
}

Result Engine::clearFx() {
    if (fxRecording_ || fxArmed_ || capturing_) return Result::Busy;
    fxUndo_ = session.fx;
    fxUndoValid_ = true;
    session.fx = FxLoop{};
    ++revision_;
    return Result::Ok;
}

Result Engine::undoFx() {
    if (!fxUndoValid_) return Result::Invalid;
    if (fxRecording_ || fxArmed_ || capturing_) return Result::Busy;
    std::swap(session.fx, fxUndo_);
    ++revision_;
    return Result::Ok;
}

void Engine::checkpoint() {
    undoTrackData_ = session.tracks[selected_];
    undoTrack_ = selected_;
    undoValid_ = true;
}

Result Engine::select(int track) {
    if (track < 0 || track >= TrackCount) return Result::Invalid;
    if (recording_ || armed_ || capturing_) return Result::Busy;
    noteOff();
    selected_ = track;
    return Result::Ok;
}

uint16_t Engine::quantize(uint16_t tick) const {
    return ((tick + TicksPerStep / 2) / TicksPerStep * TicksPerStep) % LoopTicks;
}

Result Engine::noteOn(int pad) {
    if (pad < 0 || pad > 7) return Result::Invalid;
    if (capturing_) return Result::Busy;
    if (selected_ == 3 && session.sampleCount == 0) return Result::NoSample;
    noteOff();
    livePad_ = pad;
    liveSliced_ = selected_ == 3 && session.slices;
    liveHeld_ = false;
    liveBeatUnits_ = 0;
    recordedStart_ = quantize(tick_);
    trigger(selected_, pad, 108, 0, true, -1, false, liveSliced_);
    return recording_ && session.tracks[selected_].count >= MaxNotes ? Result::Full : Result::Ok;
}

void Engine::finishLiveNote() {
    if (livePad_ < 0 || !recording_) return;
    Track &t = session.tracks[selected_];
    uint8_t recordedPad = uint8_t(livePad_) | (liveSliced_ ? 128 : 0);
    uint64_t duration = (liveBeatUnits_ * 96 + SampleRate * 30) / (SampleRate * 60);
    duration = std::min<uint64_t>(duration, LoopTicks);
    uint16_t ticks = clamp<int>(int((duration + 12) / 24 * 24), 6, LoopTicks);
    if (selected_ < 2 && !liveHeld_ && liveBeatUnits_ < HitDuration - HitFade)
        ticks = 2 * TicksPerStep;
    for (int i = 0; i < t.count; ++i) {
        if (t.notes[i].tick == recordedStart_ && t.notes[i].pad == recordedPad) {
            t.notes[i].duration = ticks;
            ++revision_;
            return;
        }
    }
    if (t.count < MaxNotes) {
        Note &n = t.notes[t.count++];
        n.tick = recordedStart_;
        n.duration = ticks;
        n.pad = recordedPad;
        n.velocity = 108;
        ++revision_;
    }
}

void Engine::noteOff() {
    finishLiveNote();
    for (Voice &v : voices_) {
        if (!v.active || !v.live) continue;
        if (v.track < 2 && !v.gate) v.released = true;
        v.live = false;
    }
    livePad_ = -1;
}

void Engine::stop() {
    noteOff();
    playing_ = recording_ = armed_ = false;
    preparation_ = RecordPreparation::None;
    preparationLoop_ = 0;
    preparationStartedPlayback_ = false;
    if (fxRecording_) finishFxRecording();
    fxRecording_ = fxArmed_ = fxTouched_ = false;
    nextScene_ = -1;
    replaceRecording_ = false;
    tick_ = 0;
    phase_ = 0;
    for (Voice &v : voices_) v.released = true;
}

void Engine::beginPreparation() {
    preparation_ = RecordPreparation::Waiting;
    preparationLoop_ = 0;
    preparationStartedPlayback_ = !playing_;
    if (!playing_) {
        playing_ = true;
        tick_ = 0;
        phase_ = 0;
        processTick();
    }
}

void Engine::cancelPreparation() {
    preparation_ = RecordPreparation::None;
    preparationLoop_ = 0;
    bool stopPlayback = preparationStartedPlayback_;
    preparationStartedPlayback_ = false;
    if (stopPlayback) stop();
}

void Engine::togglePlay() {
    if (capturing_) return;
    if (playing_) stop();
    else {
        playing_ = true;
        tick_ = 0;
        phase_ = 0;
        processTick();
    }
}

Result Engine::toggleRecord(bool replace) {
    if (capturing_ || fxRecording_ || fxArmed_ || nextScene_ >= 0) return Result::Busy;
    if (selected_ == 3 && session.sampleCount == 0) return Result::NoSample;
    if (!replace && session.tracks[selected_].count >= MaxNotes && !recording_ && !armed_)
        return Result::Full;
    if (recording_) {
        noteOff();
        recording_ = false;
    } else if (armed_) {
        armed_ = false;
        replaceRecording_ = false;
        cancelPreparation();
    } else {
        noteOff();
        checkpoint();
        replaceRecording_ = replace;
        armed_ = true;
        beginPreparation();
    }
    return Result::Ok;
}

void Engine::setBpm(int bpm) { session.bpm = clamp(bpm, 60, 180); ++revision_; }
void Engine::setVolume(int volume) { session.volume = clamp(volume, 0, 80); ++revision_; }
void Engine::setPatch(int patch) {
    session.tracks[selected_].patch = clamp(patch, 0, SoundCount - 1);
    ++revision_;
}
Result Engine::previewSound(int patch) {
    if (patch < 0 || patch >= SoundCount || selected_ >= 2) return Result::Invalid;
    if (capturing_) return Result::Busy;
    bool available = false;
    for (const Voice &v : voices_) if (!v.active || v.preview) { available = true; break; }
    if (!available) return Result::Busy;
    stopPreview();
    trigger(selected_, 0, 108, 0, false, patch, true);
    return Result::Ok;
}
void Engine::stopPreview() {
    for (Voice &v : voices_) if (v.preview) v.released = true;
}
void Engine::setTone(int tone) {
    session.tracks[selected_].tone = clamp(tone, 0, 100);
    for (Voice &v : voices_) if (v.active && v.track == selected_) tune(v);
    ++revision_;
}
void Engine::setPitch(int semitones) {
    int pitch = clamp(semitones, -12, 12);
    if (session.tracks[selected_].pitch == pitch) return;
    session.tracks[selected_].pitch = pitch;
    for (Voice &v : voices_) if (v.active && v.track == selected_) tune(v);
    ++revision_;
}
void Engine::setSpace(int space) {
    session.tracks[selected_].space = clamp(space, 0, 100); ++revision_;
}
void Engine::setScale(int scale) { noteOff(); session.scale = clamp(scale, 0, 2); ++revision_; }
void Engine::setRoot(int root) { noteOff(); session.root = clamp(root, 0, 11); ++revision_; }
void Engine::setSwing(int swing) { session.swing = clamp(swing, 0, 40); ++revision_; }
void Engine::mute(int track) {
    if (track < 0 || track >= TrackCount) return;
    session.tracks[track].muted = !session.tracks[track].muted;
    if (session.tracks[track].muted)
        for (Voice &v : voices_) if (v.track == track && !v.live) v.released = true;
    ++revision_;
}

void Engine::clearSelected() {
    noteOff();
    recording_ = false;
    if (armed_) {
        armed_ = false;
        cancelPreparation();
        replaceRecording_ = false;
    }
    checkpoint();
    session.tracks[selected_].count = 0;
    for (Voice &v : voices_) if (v.track == selected_) v.released = true;
    ++revision_;
}

void Engine::undo() {
    if (!undoValid_) return;
    noteOff();
    recording_ = false;
    if (armed_) {
        armed_ = false;
        cancelPreparation();
        replaceRecording_ = false;
    }
    std::swap(session.tracks[undoTrack_], undoTrackData_);
    for (Voice &v : voices_) if (v.track == undoTrack_) v.released = true;
    ++revision_;
}

namespace {
void arrangePopScene(Scene &scene, int song, int section) {
    struct Hook { uint8_t step, pad; };
    constexpr Hook hooks[6][14] = {
        {{0,5},{3,5},{6,4},{8,3},{11,2},{14,3},{16,5},{19,6},{22,5},{24,4},{27,2},{30,0}},
        {{0,2},{2,3},{5,4},{7,3},{10,2},{12,0},{16,2},{18,3},{21,5},{23,4},{26,3},{30,0}},
        {{0,4},{4,5},{7,7},{10,6},{12,5},{15,3},{16,4},{20,5},{23,6},{26,5},{28,2},{30,0}},
        {{0,5},{2,5},{3,3},{6,4},{8,5},{10,6},{14,4},{16,5},{18,5},{19,3},{22,2},{24,4},{27,3},{30,0}},
        {{1,2},{4,4},{6,5},{7,4},{10,3},{13,2},{16,4},{18,5},{21,6},{22,5},{25,4},{28,3},{30,2},{31,0}},
        {{0,3},{3,5},{4,5},{7,4},{8,3},{11,2},{14,0},{16,3},{19,5},{20,6},{23,5},{24,4},{27,2},{30,0}},
    };
    constexpr uint8_t hookCounts[] = {12,12,12,14,14,14};
    constexpr uint8_t progressions[6][4] = {
        {0,1,2,3}, {0,2,1,3}, {1,0,2,3}, {0,1,2,3}, {1,2,0,3}, {0,2,1,3}
    };
    constexpr uint8_t voicings[4][3] = {{0,2,3}, {2,4,5}, {3,5,6}, {2,3,5}};
    constexpr uint8_t chordRoots[] = {0,4,3,0};
    constexpr uint8_t patches[6][4] = {
        {0,0,7,0}, {8,8,5,0}, {5,0,0,7},
        {15,8,27,11}, {9,9,16,15}, {8,8,7,9}
    };
    constexpr uint8_t bassPatches[] = {22,22,22,21,21,22};
    bool disco = song >= 3, intro = section == 0, groove = section == 1, quiet = section == 2;
    auto add = [&](int track, int step, int pad, int duration, int velocity, int offset = 0) {
        Track &target = scene.tracks[track];
        target.notes[target.count++] = Note{
            uint16_t(step * TicksPerStep + offset), uint16_t(duration),
            uint8_t(pad), uint8_t(velocity)};
    };
    scene.tracks[0].patch = patches[song][section];
    scene.tracks[0].tone = disco ? 88 : 95;
    scene.tracks[0].space = quiet ? 16 : disco ? 9 : 6;
    scene.tracks[1].patch = bassPatches[song];
    scene.tracks[1].tone = disco ? 78 : 90;
    scene.tracks[1].space = 0;
    scene.tracks[1].muted = intro && !disco;
    scene.tracks[2].tone = 82;
    scene.tracks[2].space = disco ? 10 : 3;
    scene.tracks[3].tone = 100;
    scene.tracks[3].space = disco ? 18 : 8;
    scene.tracks[3].muted = intro;

    for (int group = 0; group < 4; ++group) {
        int chord = progressions[song][group];
        const uint8_t *voicing = voicings[chord];
        int step = group * 8;
        if (intro || quiet) {
            for (int note = 0; note < 3; ++note)
                add(0, step + note * 2, voicing[note],
                    (quiet ? 5 : 2) * TicksPerStep, 72 + note * 7);
        } else {
            int strum = disco ? 2 : 0;
            int notes = !disco && !groove ? 3 : 2;
            for (int note = 0; note < notes; ++note)
                add(0, step + strum, voicing[note], disco ? 18 : 2 * TicksPerStep,
                    72 - note * 7, note * (disco ? 0 : 2));
            if (!disco || groove)
                for (int note = 1; note < 3; ++note)
                    add(0, step + (song == 1 ? 5 : 6), voicing[note],
                        disco ? 12 : TicksPerStep, 52 + note * 5, (2 - note) * 2);
        }
        int root = chordRoots[chord];
        int fifth = root == 0 ? 3 : root == 3 ? 6 : 7;
        add(1, step, root, (quiet ? 5 : 1) * TicksPerStep, quiet ? 84 : 106);
        if (!intro && !quiet) {
            if (disco) {
                add(1, step + 3, root, 12, 72);
                add(1, step + 6, fifth, TicksPerStep, 96);
                add(1, step + 7, root == 0 ? 5 : fifth, 12, 84);
                if (!groove && group % 2) add(1, step + 2, root, 12, 62);
            } else {
                add(1, step + 6, fifth, TicksPerStep, 84);
                if (!groove) {
                    int next = chordRoots[progressions[song][(group + 1) % 4]];
                    add(1, step + 7, next, TicksPerStep, 70);
                }
            }
        }
    }
    if (!intro) {
        for (int i = 0; i < hookCounts[song]; ++i) {
            if ((quiet && i % 3) || (groove && i % 2)) continue;
            const Hook &hook = hooks[song][i];
            int pad = groove ? (hook.pad + 2) % 8 : hook.pad;
            add(0, hook.step, pad, (quiet ? 6 : disco ? 1 : 2) * TicksPerStep,
                quiet ? 84 : i % 3 == 0 ? 112 : 96);
        }
    }
    for (int step = 0; step < 32; ++step) {
        bool kick = disco ? step % 4 == 0 :
            song == 1 ? step == 0 || step == 10 || step == 16 || step == 26 :
            step % 8 == 0 || (song == 0 && (step == 6 || step == 22));
        if (intro || quiet) kick = step == 0 || step == 16;
        if (kick) add(2, step, 0, TicksPerStep, quiet ? 64 : disco ? 112 : 98);
        if (!intro && !quiet && step % 8 == 4) {
            add(2, step, song == 1 && groove ? 7 : 1, TicksPerStep, disco ? 104 : 80);
            if (disco) add(2, step, 4, TicksPerStep, groove ? 54 : 76);
        } else if (quiet && (step == 12 || step == 28)) {
            add(2, step, 7, TicksPerStep, 48);
        }
        if (step % (intro || quiet ? 4 : 2) == 0) {
            int hat = disco && !intro && !quiet && step % 4 == 2 ? 3 : 2;
            add(2, step, hat, TicksPerStep,
                quiet ? 24 : hat == 3 ? 67 : step % 4 ? 48 : 32);
        }
        if (!intro && !quiet && step % 2 && (disco || song == 1))
            add(2, step, 6, TicksPerStep, step % 4 == 3 ? 34 : 24);
        if (section == 3 && (step == 29 || step == 31))
            add(2, step, disco ? 4 : 5, TicksPerStep, step == 31 ? 70 : 50);
    }
    add(3, quiet ? 8 : 2, 0, TicksPerStep, 112);
    add(3, quiet ? 24 : 18, groove ? 0 : 1, TicksPerStep, 98);
    if (section == 3) add(3, 29, disco ? 2 : 0, TicksPerStep, 102);
    if (song == 4 && section == 3) {
        scene.fx.enabled = true;
        for (int point = 56; point < FxSteps; ++point)
            scene.fx.points[point] = FxPoint{100, uint8_t(12 + (point - 56) * 3), true};
    }
}
}

Result Engine::loadDemo(int demo) {
    if (demo < 0 || demo >= DemoCount) return Result::Invalid;
    if (capturing_) return Result::Busy;
    stop();
    for (Voice &v : voices_) v.active = false;
    std::memset(delay_, 0, sizeof(delay_));
    std::memset(fxDelay_, 0, sizeof(fxDelay_));
    selected_ = 0;
    session.bpm = DemoLibrary[demo].bpm;
    constexpr uint8_t roots[] = {0, 2, 7, 5, 0, 2, 7, 2, 0, 0, 9, 5};
    constexpr uint8_t lead[] = {0, 10, 15, 11, 17, 4};
    constexpr uint8_t bass[] = {22, 22, 14, 28, 21, 23};
    constexpr uint8_t alternate[] = {5, 7, 19, 24, 16, 18};
    constexpr uint8_t intimate[] = {7, 9, 29, 28, 27, 8};
    constexpr uint8_t melodies[6][16] = {
        {0,2,4,3,5,4,2,1,0,2,4,6,5,3,2,0},
        {2,4,3,1,0,2,1,4,5,4,2,3,1,2,1,0},
        {0,2,4,5,4,2,1,3,5,6,4,3,2,4,1,0},
        {0,0,2,4,2,1,0,3,4,4,5,4,2,1,2,0},
        {0,4,3,2,0,5,4,2,1,4,6,4,3,2,4,0},
        {0,0,3,0,4,3,2,0,5,5,3,4,2,0,1,0},
    };
    constexpr uint8_t syncopated[] = {0,3,4,6,8,11,12,14,16,19,20,22,24,27,28,30};
    session.root = roots[demo];
    session.scale = demo >= 6 || demo == 0 || demo == 3 ? 1 : demo == 2 ? 2 : 0;
    constexpr uint8_t newSwing[] = {5,14,3,7,4,10};
    session.swing = demo >= 6 ? newSwing[demo - 6] : demo == 1 ? 34 : demo == 0 ? 9 : 0;
    session.sampleCount = DemoVoices[demo].count;
    std::copy_n(DemoVoices[demo].samples, session.sampleCount, sample);
    for (int section = 0; section < SceneCount; ++section) {
        Scene &scene = session.scenes[section];
        scene = Scene{};
        if (demo >= 6) { arrangePopScene(scene, demo - 6, section); continue; }
        bool intro = section == 0, groove = section == 1, quiet = section == 2;
        auto add = [&](int track, int step, int pad, int length, int velocity) {
            Track &t = scene.tracks[track];
            Note &n = t.notes[t.count++];
            n = Note{uint16_t(step * TicksPerStep), uint16_t(length * TicksPerStep),
                     uint8_t(pad), uint8_t(velocity)};
        };
        scene.tracks[0].patch = quiet ? intimate[demo] : intro ? alternate[demo] : lead[demo];
        scene.tracks[1].patch = quiet && demo == 3 ? 13 : bass[demo];
        scene.tracks[0].tone = 92;
        scene.tracks[0].space = demo == 2 ? 34 : demo >= 4 ? 20 : 10;
        scene.tracks[1].tone = 82;
        scene.tracks[1].space = 3;
        scene.tracks[2].tone = 80;
        scene.tracks[2].space = demo == 2 ? 18 : 4;
        scene.tracks[3].tone = 100;
        scene.tracks[3].space = demo >= 4 ? 22 : 8;
        scene.tracks[3].pitch = demo == 1 ? -3 : demo == 5 ? -5 : 0;
        if (demo == 5) scene.tracks[0].pitch = -12;
        scene.tracks[0].muted = intro;
        scene.tracks[3].muted = intro;
        int stride = quiet || intro || demo == 2 ? 4 : 2;
        for (int step = 0; step < 32; step += stride) {
            int position = step / 2;
            int when = demo == 0 || demo == 1 ? syncopated[position] : step;
            int pad = melodies[demo][(position + (quiet ? 4 : groove ? 2 : 0)) % 16];
            add(0, when, pad, demo == 2 ? 5 : quiet ? 3 : 1,
                quiet ? 80 : position % 4 == 0 ? 112 : position % 3 == 0 ? 76 : 96);
            if (!intro && !quiet && !groove && demo == 0 && step % 8 == 0)
                add(0, when, (pad + 2) % 8, 2, 64);
        }
        for (int step = 0; step < 32; step += quiet ? 8 : demo >= 4 ? 2 : 4) {
            int pad = demo == 3 ? ((step / 4) % 2 ? 3 : 0) :
                demo == 1 ? (step / 4 + section) % 5 : (step / 8) % 4;
            int when = demo == 4 && !quiet ? step + 1 : step;
            add(1, when, pad, quiet ? 6 : demo == 2 ? 5 : 1,
                step % 8 ? 89 : 112);
        }
        for (int step = 0; step < 32; ++step) {
            bool fourFloor = demo == 4 || demo == 3;
            bool kick = step % (fourFloor ? 4 : 8) == 0 ||
                (!intro && !quiet && (step == 10 || step == 26 || (demo == 5 && step == 15)));
            if (quiet) kick = step == 0 || step == 16;
            if (kick) add(2, step, 0, 1, quiet ? 64 : 116);
            if (!quiet && step % 8 == 4) {
                add(2, step, 1, 1, demo == 1 ? 65 : 100);
                if (!intro && demo >= 4) add(2, step, 4, 1, 48);
            }
            if (step % (quiet ? 8 : 2) == 0)
                add(2, step, demo == 1 ? 6 : 2, 1, step % 4 == 2 ? 65 : 36);
            if (!intro && !groove && !quiet && (step == 29 || step == 31))
                add(2, step, 5, 1, step == 31 ? 86 : 62);
        }
        add(3, quiet ? 8 : 0, quiet ? 2 : 0, 1, 108);
        add(3, 16, groove ? 1 : 3, 1, 94);
        if (!quiet && !intro) add(3, 27, demo % 2 ? 4 : 2, 1, 104);
        if (demo == 4 && section == 3) {
            scene.fx.enabled = true;
            for (int point = 48; point < FxSteps; ++point)
                scene.fx.points[point] = FxPoint{uint8_t(55 + (point - 48) * 3),
                                                uint8_t((point - 48) * 3), true};
        }
    }
    session.activeScene = 3;
    std::copy_n(session.scenes[3].tracks, TrackCount, session.tracks);
    session.fx = session.scenes[3].fx;
    session.slices = false;
    undoValid_ = false;
    fxUndoValid_ = false;
    ++revision_;
    return Result::Ok;
}

Result Engine::startCapture() {
    if (capturing_) return Result::Busy;
    stop();
    for (Voice &v : voices_) v.active = false;
    std::memset(delay_, 0, sizeof(delay_));
    std::memset(fxDelay_, 0, sizeof(fxDelay_));
    session.sampleCount = 0;
    captureCount_ = 0;
    captureSkip_ = SampleRate / 5;
    capturing_ = true;
    return Result::Ok;
}

Result Engine::audition() {
    if (capturing_) return Result::Busy;
    if (!session.sampleCount) return Result::NoSample;
    trigger(3, 0, 108, session.sampleCount, false);
    return Result::Ok;
}

void Engine::capture(const int16_t *frames, size_t count, int stride, int channel) {
    if (!capturing_ || stride < 1 || channel < 0 || channel >= stride) return;
    for (size_t i = 0; i < count && capturing_; ++i) {
        if (captureSkip_ > 0) { --captureSkip_; continue; }
        sample[captureCount_++] = frames[i * stride + channel];
        if (captureCount_ == SampleCapacity) finishCapture();
    }
}

void Engine::finishCapture() {
    if (!capturing_) return;
    capturing_ = false;
    session.sampleCount = captureCount_;
    if (captureCount_ < SampleRate / 10) { session.sampleCount = 0; ++revision_; return; }
    int64_t sum = 0;
    for (uint32_t i = 0; i < captureCount_; ++i) sum += sample[i];
    int mean = int(sum / captureCount_);
    int maximum = 1;
    for (uint32_t i = 0; i < captureCount_; ++i)
        maximum = std::max(maximum, std::abs(int(sample[i]) - mean));
    float gain = std::min(32.0f, 26000.0f / maximum);
    for (uint32_t i = 0; i < captureCount_; ++i) {
        float fade = std::min(1.0f, std::min(i, captureCount_ - 1 - i) / 80.0f);
        sample[i] = int16_t(clamp((sample[i] - mean) * gain * fade, -30000.0f, 30000.0f));
    }
    ++revision_;
}

int32_t Engine::oscillator(uint32_t phase) const {
    return sine_[phase >> 22];
}

int32_t Engine::noise() {
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return int32_t(noise_ & 65535) - 32768;
}

void Engine::trigger(int track, int pad, int velocity, uint32_t gate, bool live,
                     int patch, bool preview, bool sliced) {
    if (track == 3 && !session.sampleCount) return;
    Voice *slot = nullptr;
    for (Voice &v : voices_) if (!v.active) { slot = &v; break; }
    if (!slot) for (Voice &v : voices_) if (v.preview) { slot = &v; break; }
    if (!slot) {
        for (Voice &v : voices_)
            if (!v.live && (!slot || v.envelope < slot->envelope)) slot = &v;
        if (!slot) slot = &voices_[0];
    }
    *slot = Voice{};
    slot->active = true;
    slot->live = live;
    slot->preview = preview;
    slot->sliced = track == 3 && sliced;
    slot->track = track;
    slot->pad = pad;
    slot->patch = patch < 0 ? session.tracks[track].patch : patch;
    slot->gate = track >= 2 ? 0 :
        live || preview ? HitDuration : gate * (SampleRate * 60 / 96);
    slot->velocity = velocity;
    int midi = midiNote(track, pad, session.root, session.scale);
    slot->midi = midi;
    if (track < 2) {
        if (Sounds[slot->patch].wave == SoundWave::Sample) {
            slot->instrument = findInstrumentSample(slot->patch,
                instrumentMidi(slot->patch, midi + session.tracks[track].pitch), velocity);
        }
        slot->hitAttack = std::min<uint32_t>(Sounds[slot->patch].attack,
            slot->gate / session.bpm / 6);
    }
    slot->increment = uint32_t(440.0 * std::pow(2.0, (midi - 69) / 12.0) *
        (4294967296.0 / SampleRate));
    slot->increment2 = uint32_t(slot->increment * Sounds[slot->patch].ratio);
    if (track == 3) {
        slot->sampleStart = slot->sliced ? uint32_t(pad) * session.sampleCount / 8 : 0;
        slot->sampleEnd = slot->sliced ? uint32_t(pad + 1) * session.sampleCount / 8 : session.sampleCount;
        slot->samplePosition = slot->sampleStart << 16;
    }
    tune(*slot);
}

void Engine::tune(Voice &v) {
    int pitch = session.tracks[v.track].pitch;
    v.pitchRatio = uint32_t(65536.0 * std::pow(2.0, pitch / 12.0));
    if (v.track < 2) {
        int midi = clamp<int>(v.midi + pitch, 36, 100);
        if (v.instrument) {
            int recordedMidi = instrumentMidi(v.patch, midi);
            v.sampleIncrement = uint32_t(65536.0 * std::pow(2.0, (recordedMidi - v.instrument->root) / 12.0));
            uint32_t cutoff = uint64_t(61604) * 65536 / std::max<uint32_t>(65536, v.sampleIncrement);
            v.resampler = 7;
            for (int band = 0; band < 8; ++band)
                if (cutoff >= InstrumentCutoffs[band]) { v.resampler = band; break; }
        }
        const SpeakerBody &body = SpeakerBodies[v.patch];
        v.presence = body.gain * clamp(84 - midi, 0, 24) / 24;
        int multiplier = midi < 48 ? 2 : 1;
        uint32_t increment = uint64_t(v.increment) * v.pitchRatio >> 16;
        int highest = std::max<uint32_t>(1, 2013265920u / increment);
        v.body1 = std::min(int(body.first) * multiplier - (multiplier - 1), highest);
        v.body2 = std::min(int(body.second) * multiplier - (multiplier - 1), highest);
        if (Sounds[v.patch].wave == SoundWave::Choir) {
            double frequency = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            v.body1 = clamp<int>(int(500 / frequency + 0.5), 1, highest);
            v.body2 = clamp<int>(int(1500 / frequency + 0.5), 1, highest);
            v.presence = 0;
        }
#ifndef ORBIT_CALIBRATE
        const uint16_t *levels = SoundLevels[v.track][v.patch][midi - 36];
        int tone = session.tracks[v.track].tone;
        int segment = std::min(tone / 20, 4);
        int offset = tone - segment * 20;
        v.level = levels[segment] + (int(levels[segment + 1]) - levels[segment]) * offset / 20;
        if (Sounds[v.patch].wave == SoundWave::Arcade) {
            const auto &arcade = ArcadeLevels[v.track][midi - 36];
            for (int step = 0; step < 3; ++step)
                v.arcadeLevels[step] = arcade[segment][step] +
                    (int(arcade[segment + 1][step]) - arcade[segment][step]) * offset / 20;
        }
#endif
    }
    if (v.track == 3)
        v.sampleIncrement = uint32_t(65536.0 * std::pow(2.0,
            ((v.sliced ? 0 : midiNote(3, v.pad, 0, session.scale) - 60) + pitch) / 12.0));
}

void Engine::processTick() {
    if (tick_ == 0) {
        if (preparation_ == RecordPreparation::Waiting) {
            preparation_ = RecordPreparation::GetReady;
            preparationLoop_ = 1;
            for (Voice &v : voices_) if (!v.live) v.released = true;
        } else if (preparation_ != RecordPreparation::None && ++preparationLoop_ > 2) {
            preparation_ = preparation_ == RecordPreparation::GetReady ?
                RecordPreparation::Practice : RecordPreparation::None;
            preparationLoop_ = preparation_ == RecordPreparation::Practice ? 1 : 0;
            if (preparation_ == RecordPreparation::None) preparationStartedPlayback_ = false;
        }
        if (nextScene_ >= 0) applyScene(nextScene_);
        if (fxRecording_) {
            finishFxRecording();
        } else if (fxArmed_ && preparation_ == RecordPreparation::None) {
            fxArmed_ = false;
            fxRecording_ = true;
            fxUndo_ = session.fx;
            fxUndoValid_ = true;
            session.fx = FxLoop{};
            updateFxRecording();
        }
    }
    if (tick_ % (LoopTicks / FxSteps) == 0) updateFxRecording();
    if (tick_ == 0 && armed_ && preparation_ == RecordPreparation::None) {
        armed_ = false;
        recording_ = true;
        if (session.tracks[selected_].muted) {
            session.tracks[selected_].muted = false;
            ++revision_;
        }
        if (replaceRecording_) {
            session.tracks[selected_].count = 0;
            for (Voice &v : voices_)
                if (v.track == selected_ && !v.live) v.released = true;
            replaceRecording_ = false;
            ++revision_;
        }
        if (livePad_ >= 0) { liveBeatUnits_ = 0; recordedStart_ = 0; }
    }
    if ((preparation_ == RecordPreparation::GetReady || preparation_ == RecordPreparation::Practice) &&
        tick_ % 96 == 0) {
        trigger(2, 7, 55, 800, false);
    }
    for (int track = 0; track < TrackCount; ++track) {
        const Track &t = session.tracks[track];
        if (t.muted || preparation_ == RecordPreparation::GetReady ||
            (preparation_ == RecordPreparation::Practice && replaceRecording_ && track == selected_))
            continue;
        for (int i = 0; i < t.count; ++i) {
            const Note &n = t.notes[i];
            uint16_t swing = ((n.tick / TicksPerStep) & 1) ?
                session.swing * TicksPerStep / 100 : 0;
            if ((n.tick + swing) % LoopTicks == tick_ &&
                !(recording_ && track == selected_ && livePad_ == (n.pad & 127) &&
                    liveSliced_ == bool(n.pad & 128))) {
                trigger(track, n.pad & 127, n.velocity, n.duration, false, -1, false, n.pad & 128);
            }
        }
    }
}

int32_t Engine::voiceSample(Voice &v) {
    const SoundPreset &sound = Sounds[v.patch];
    ++v.age;
    if (v.gate) {
        v.hitProgress += session.bpm;
        if (v.live && v.hitProgress >= HitDuration - HitFade) {
            v.gate = 0;
            liveHeld_ = true;
        }
        else if (v.hitProgress >= v.gate) { v.active = false; return 0; }
    }
    if (v.released) {
        v.envelope = std::max<int32_t>(0, v.envelope -
            (v.track < 2 ? 65536 / (SampleRate / 125) :
             std::max<int32_t>(1, v.envelope >> 8)));
    }
    else if (v.track == 2) {
        int shift = v.pad == 2 || v.pad == 6 || v.pad == 7 ? 8 :
            (v.pad == 3 ? 11 : 10);
        v.envelope -= std::max<int32_t>(1, v.envelope >> shift);
    } else if (v.track < 2 && !v.instrument && sound.decayShift) {
        v.envelope -= std::max<int32_t>(1, v.envelope >> sound.decayShift);
    }
    v.timbre -= std::max<int32_t>(1, v.timbre >> 11);
    v.timbre = std::max<int32_t>(0, v.timbre);
    int32_t value = 0;
    if (v.track == 3) {
        uint32_t index = v.samplePosition >> 16;
        if (index >= v.sampleEnd || index + 1 >= session.sampleCount) { v.active = false; return 0; }
        int32_t fraction = v.samplePosition & 65535;
        value = sample[index] +
            int32_t((int64_t(sample[std::min(index + 1, v.sampleEnd - 1)] - sample[index]) * fraction) >> 16);
        if (v.sliced) {
            uint32_t fade = std::min<uint32_t>(64, std::min(index - v.sampleStart, v.sampleEnd - 1 - index));
            value = value * int32_t(fade) / 64;
        }
        v.samplePosition += v.sampleIncrement;
    } else if (v.track == 2) {
        int32_t n = noise();
        switch (v.pad) {
        case 0:
            v.increment = 18790482u + uint32_t((int64_t(37580964) * v.timbre) >> 16);
            value = (oscillator(v.phase) * 2 + oscillator(v.phase * 3) +
                oscillator(v.phase * 5)) / 4;
            if (v.age < 128) value += n * (128 - int(v.age)) / 512;
            break;
        case 1:
            value = (n * 7 + oscillator(v.phase) * 3) / 10;
            v.increment = 48318382u;
            break;
        case 2: value = n / 2; break;
        case 3: value = n / 3; break;
        case 4:
            value = (v.age / 110) % 2 ? n / 3 : n;
            break;
        case 5:
            v.increment = 32212254u + uint32_t((int64_t(26843545) * v.timbre) >> 16);
            value = (oscillator(v.phase) * 2 + oscillator(v.phase * 3) +
                oscillator(v.phase * 5)) / 4;
            break;
        case 6: value = n * 2 / 3; break;
        default: value = oscillator(v.phase) / 2;
            v.increment = 429496729u; break;
        }
    } else if (v.instrument) {
        const InstrumentSample &source = *v.instrument;
        uint32_t index = v.samplePosition >> 16;
        bool looping = source.loopEnd;
        if (looping && index >= source.loopEnd) {
            v.samplePosition -= (source.loopEnd - source.loopStart) << 16;
            index = v.samplePosition >> 16;
            v.looped = true;
        }
        if (index >= source.frames) { v.active = false; return 0; }
        const int16_t *coefficients = InstrumentResampler[v.resampler][(v.samplePosition & 65535) >> 11];
        int32_t sum = 0;
        uint32_t lower = looping && v.looped ? source.loopStart : 0;
        uint32_t upper = looping ? source.loopEnd : source.frames;
        if (index >= lower + 7 && index + 8 < upper) {
            const uint8_t *input = source.data + index - 7;
            for (int tap = 0; tap < 16; ++tap)
                sum += InstrumentDecode[input[tap]] * coefficients[tap];
        } else {
            for (int tap = 0; tap < 16; ++tap) {
                int position = int(index) + tap - 7;
                if (looping && position >= int(source.loopEnd))
                    position -= source.loopEnd - source.loopStart;
                else if (looping && v.looped && position < int(source.loopStart))
                    position += source.loopEnd - source.loopStart;
                position = clamp<int>(position, 0, source.frames - 1);
                sum += InstrumentDecode[source.data[position]] * coefficients[tap];
            }
        }
        value = sum / 32768;
        v.samplePosition += v.sampleIncrement;
    } else {
        int32_t triangle = 32767 - std::abs(int32_t(v.phase >> 16) - 32768) * 2;
        uint32_t increment = uint64_t(v.increment) * v.pitchRatio >> 16;
        uint32_t increment2 = uint64_t(v.increment2) * v.pitchRatio >> 16;
        int32_t saw = bandlimitedSaw(v.phase, increment);
        switch (sound.wave) {
        case SoundWave::Pluck:
            value = (oscillator(v.phase) * 3 + oscillator(v.phase * 2)) / 4;
            break;
        case SoundWave::Warm:
            value = (triangle * 15 + oscillator(v.phase2)) / 16;
            break;
        case SoundWave::Bell:
        case SoundWave::Rubber:
            value = oscillator(v.phase + uint32_t(
                int64_t(oscillator(v.phase2)) * (32768 + v.timbre / 2) * sound.character / 256));
            break;
        case SoundWave::Acid:
            value = saw * 2 / 3 +
                (saw - bandlimitedSaw(v.phase + 2662879724u, increment)) * 6000 / 32768;
            break;
        case SoundWave::Guitar:
            value = int32_t((int64_t(oscillator(v.phase * 3) + oscillator(v.phase * 5)) *
                v.timbre) >> 17);
            value = (oscillator(v.phase) * 5 + value) / 6;
            break;
        case SoundWave::Harp:
            value = (oscillator(v.phase) * 7 + oscillator(v.phase * 3)) / 8;
            break;
        case SoundWave::Piano:
            value = int32_t((int64_t(oscillator(v.phase * 2) + oscillator(v.phase * 3)) *
                v.timbre) >> 17);
            value = (oscillator(v.phase) * 5 + oscillator(v.phase * 2) + value * 2) / 8;
            break;
        case SoundWave::Organ:
            value = (oscillator(v.phase) * 3 + oscillator(v.phase * 2) * 2 +
                oscillator(v.phase * 3) + oscillator(v.phase * 4) + oscillator(v.phase * 8)) / 8;
            break;
        case SoundWave::Pad:
            if (sound.character) {
                int32_t shimmer = oscillator(v.phase2 + uint32_t(
                    int64_t(oscillator(v.phase * 4)) * (16384 + v.timbre / 2) / 8));
                value = (oscillator(v.phase) * 2 + shimmer * 3 + oscillator(v.phase * 4)) / 6;
                value = int32_t(int64_t(value) *
                    (24576 + oscillator(v.age * 671089u) / 4) >> 15);
            } else {
                value = (saw + int32_t(v.phase2 >> 16) - 32768 + triangle * 2 +
                    oscillator(v.phase * 3) * 2 + oscillator(v.phase * 5)) / 7;
            }
            break;
        case SoundWave::Saw:
            value = (saw + bandlimitedSaw(v.phase2, increment2)) / 2;
            break;
        case SoundWave::Square:
            value = (saw - bandlimitedSaw(v.phase + 2684354560u, increment)) * 24000 / 32768;
            break;
        case SoundWave::Flute:
            value = (oscillator(v.phase) * 11 + oscillator(v.phase2) * 3 +
                oscillator(v.phase * 3) + noise() / 8) / 15;
            break;
        case SoundWave::Sub:
            value = (oscillator(v.phase) * 5 + oscillator(v.phase2) * 2 +
                oscillator(v.phase * 2)) / 8;
            break;
        case SoundWave::Growl:
            value = (saw + int32_t(int64_t(oscillator(v.phase)) * oscillator(v.phase2) >> 15)) / 2;
            break;
        case SoundWave::Chip:
            value = ((sound.character ? saw : triangle) / 4096) * 4096;
            break;
        case SoundWave::Arcade: {
            uint32_t octave = 1u << ((v.age / 800) % 3);
            value = v.phase * octave < 2147483648u ? 22000 : -22000;
            value = (value + oscillator(v.phase) * 2) / 3;
            break;
        }
        case SoundWave::Choir:
            value = (oscillator(v.phase) * 2 + oscillator(v.phase * v.body1) * 3 +
                oscillator(v.phase2 * v.body1) * 2 + oscillator(v.phase * v.body2) * 3) / 10;
            value = int32_t(int64_t(value) *
                (28672 + oscillator(v.age * 1207960u) / 8) >> 15);
            break;
        case SoundWave::Space: {
            int depth = sound.character + (oscillator(v.age * 268435u) + 32768) / 1024;
            value = oscillator(v.phase + uint32_t(
                int64_t(oscillator(v.phase2)) * (32768 + v.timbre / 2) * depth / 256));
            break;
        }
        case SoundWave::Robot:
            value = int32_t(int64_t(oscillator(v.phase)) * oscillator(v.phase2) >> 15);
            value = value / 4096 * 4096;
            break;
        case SoundWave::Sample:
            break;
        }
        if (v.track == 1) {
            value = (value * 6 + oscillator(v.phase * 2) +
                oscillator(v.phase * 3) + oscillator(v.phase * 4)) / 8;
        }
        if (v.presence) {
            int32_t body = (oscillator(v.phase * v.body1) * 2 + oscillator(v.phase * v.body2)) / 3;
            value = (value * 256 + body * v.presence) / (256 + v.presence);
        }
        value = value * sound.gain / 128;
    }
    int tone = session.tracks[v.track].tone;
    int cutoff = v.track == 3 ? 40 + tone * 2 : 10 + tone * tone / 50;
    if (v.track < 2) cutoff = clamp(cutoff * sound.brightness / 100, 80, 240);
    if (v.track == 1 || (v.track == 2 && (v.pad == 0 || v.pad == 5)))
        cutoff = std::max(cutoff, 100);
    if (v.track < 2 && sound.sweep)
        cutoff = std::max<int32_t>(sound.wave == SoundWave::Rubber ? 140 : 80,
            cutoff * (16384 + v.timbre * 3 / 4) >> 16);
    if (!v.instrument) {
        v.filter += (value - v.filter) * cutoff >> 8;
        value = v.filter;
    }
    if (v.track < 2) {
        int level = v.level;
        if (sound.wave == SoundWave::Arcade) {
            int step = (v.age / 800) % 3;
            int previous = v.age < 800 ? step : (step + 2) % 3;
            int blend = std::min<uint32_t>(64, v.age % 800);
            level = v.arcadeLevels[previous] +
                (int(v.arcadeLevels[step]) - v.arcadeLevels[previous]) * blend / 64;
        }
        value = int32_t(int64_t(value) * level >> 12);
    }
    v.phase += uint32_t((uint64_t(v.increment) * v.pitchRatio) >> 16);
    v.phase2 += uint32_t((uint64_t(v.increment2) * v.pitchRatio) >> 16);
    if (v.envelope < 12) v.active = false;
    int32_t envelope = v.envelope;
    uint32_t fade = std::min<uint32_t>(HitFade, v.gate / 8);
    if (v.gate && v.gate - v.hitProgress < fade)
        envelope = envelope * int32_t(v.gate - v.hitProgress) / fade;
    uint32_t attack = v.track < 2 && !v.instrument ? v.hitAttack :
        v.track < 2 ? sound.attack : 64;
    if (v.age < attack) envelope = envelope * v.age / attack;
    return int32_t((int64_t(value) * envelope * v.velocity) >> 24);
}

void Engine::render(int16_t *out, size_t count) {
    int maximum = 0;
    for (size_t i = 0; i < count; ++i) {
        if (livePad_ >= 0) liveBeatUnits_ += session.bpm;
        int32_t dry = 0, send = 0;
        if (!capturing_) {
            for (Voice &v : voices_) {
                if (!v.active) continue;
                int32_t x = voiceSample(v);
                dry += x;
                send += x * session.tracks[v.track].space / 100;
            }
        }
        int32_t echo = delay_[delayPosition_];
        delay_[delayPosition_] = int16_t(clamp<int32_t>(send + echo * 3 / 8, -30000, 30000));
        delayPosition_ = (delayPosition_ + 1) & 4095;
        int32_t targetGain = capturing_ ? 0 : session.volume * 256;
        outputGain_ += (targetGain - outputGain_) / 32;
        int32_t bus = dry + echo * 2 / 5;
        FxPoint point = effectPoint();
        int targetMix = point.active ? 256 : 0;
        fxMix_ += clamp<int32_t>(targetMix - fxMix_, -4, 4);
        int targetCutoff = (6 + point.x * point.x * 250 / 10000) * 256;
        fxCutoff_ += (targetCutoff - fxCutoff_) / 64;
        fxEcho_ += (point.y * 256 - fxEcho_) / 64;
        fxFilter_ += int32_t(int64_t(bus - fxFilter_) * fxCutoff_ >> 16);
        int32_t fxEcho = fxDelay_[fxDelayPosition_];
        fxDelay_[fxDelayPosition_] = int16_t(clamp<int32_t>(
            fxMix_ ? fxFilter_ + fxEcho * fxEcho_ / 65536 : 0, -30000, 30000));
        fxDelayPosition_ = (fxDelayPosition_ + 1) & 4095;
        int32_t effected = fxFilter_ + fxEcho * fxEcho_ / 32768;
        bus += int32_t(int64_t(effected - bus) * fxMix_ >> 8);
        int32_t mixed = int32_t((int64_t(bus) * outputGain_) >> 15);
        int32_t hp = mixed - highpassInput_ + int32_t(int64_t(highpassOutput_) * 32604 / 32768);
        highpassInput_ = mixed;
        highpassOutput_ = hp;
        int32_t magnitude = std::abs(hp);
        if (magnitude > 24000) magnitude = 24000 + (magnitude - 24000) / 4;
        out[i] = int16_t(std::min<int32_t>(magnitude, 30000) * (hp < 0 ? -1 : 1));
        if (std::abs(int(out[i])) <= 4) quietSamples_ = std::min<uint32_t>(quietSamples_ + 1, SampleRate);
        else quietSamples_ = 0;
        maximum = std::max(maximum, std::abs(int(out[i])));
        if (playing_ && !capturing_) {
            phase_ += session.bpm * 96;
            if (phase_ >= SampleRate * 60) {
                phase_ -= SampleRate * 60;
                tick_ = (tick_ + 1) % LoopTicks;
                processTick();
            }
        }
    }
    peak_ = maximum;
}

Status Engine::status() const {
    Status s;
    s.playing = playing_;
    s.recording = recording_;
    s.armed = armed_;
    s.preparation = preparation_;
    s.preparationLoop = preparationLoop_;
    if (preparation_ == RecordPreparation::GetReady || preparation_ == RecordPreparation::Practice)
        s.countdown = 8 - tick_ / 96;
    s.capturing = capturing_;
    s.undo = undoValid_;
    s.selected = selected_;
    s.step = tick_ / TicksPerStep;
    s.livePad = livePad_;
    s.tick = tick_;
    s.bpm = session.bpm;
    s.volume = session.volume;
    s.root = session.root;
    s.scale = session.scale;
    s.swing = session.swing;
    s.scene = session.activeScene;
    s.nextScene = nextScene_;
    s.slices = session.slices;
    s.fxTouched = fxTouched_;
    s.fxRecording = fxRecording_;
    s.fxArmed = fxArmed_;
    s.fxEnabled = session.fx.enabled;
    s.fxUndo = fxUndoValid_;
    s.effect = effectPoint();
    s.sampleCount = session.sampleCount;
    s.captureCount = captureCount_;
    s.revision = revision_;
    s.peak = peak_;
    for (int i = 0; i < TrackCount; ++i) {
        s.counts[i] = session.tracks[i].count;
        s.patches[i] = session.tracks[i].patch;
        s.tones[i] = session.tracks[i].tone;
        s.pitches[i] = session.tracks[i].pitch;
        s.spaces[i] = session.tracks[i].space;
        s.muted[i] = session.tracks[i].muted;
    }
    for (const Voice &v : voices_)
        if (v.active && !v.released) s.activePads[v.track] |= 1 << v.pad;
    return s;
}
} // namespace orbit
