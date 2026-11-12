#pragma once

#include <stddef.h>
#include <stdint.h>
#include "sound_presets.h"
#include "sample_bank.h"

namespace orbit {

constexpr int SampleRate = 16000;
constexpr int SampleCapacity = SampleRate * 2;
constexpr int TrackCount = 4;
constexpr int MaxNotes = 96;
constexpr int TicksPerStep = 24;
constexpr int LoopTicks = 32 * TicksPerStep;
constexpr int DemoCount = 13;
constexpr int SixSevenArrangement = 12;
constexpr int SceneCount = 4;
constexpr int FxSteps = 64;
extern const char *const SceneNames[SceneCount];

struct DemoInfo {
    const char *name;
    const char *style;
    uint16_t bpm;
    uint8_t arrangement;
};
extern const DemoInfo DemoLibrary[DemoCount];

struct Note {
    uint16_t tick = 0;
    uint16_t duration = 1;
    uint8_t pad = 0;
    uint8_t velocity = 100;
};

struct Track {
    uint16_t count = 0;
    uint8_t patch = 0;
    uint8_t tone = 60;
    uint8_t space = 25;
    bool muted = false;
    Note notes[MaxNotes]{};
    int8_t pitch = 0;
    uint8_t reserved = 0;
};

struct FxPoint {
    uint8_t x = 100;
    uint8_t y = 0;
    bool active = false;
};

struct FxLoop {
    FxPoint points[FxSteps]{};
    bool enabled = false;
};

struct Scene {
    Track tracks[TrackCount]{};
    FxLoop fx;
    bool slices = false;
};

struct Session {
    uint32_t magic = 0x4f524254;
    uint16_t version = 5;
    uint16_t bpm = 110;
    uint8_t volume = 35;
    uint8_t root = 0;
    uint8_t scale = 0;
    uint8_t swing = 0;
    uint32_t sampleCount = 0;
    Track tracks[TrackCount]{};
    FxLoop fx;
    bool slices = false;
    uint8_t activeScene = 3;
    Scene scenes[SceneCount]{};
};

static_assert(offsetof(Track, pitch) == 582, "Keep the version-1 track prefix intact");
static_assert(offsetof(Track, reserved) == 583 && sizeof(Track) == 584,
              "Keep the saved track layout intact");
static_assert(offsetof(Session, tracks) == 16, "Keep the saved session header intact");
constexpr size_t LegacySessionBytes = offsetof(Session, fx);
static_assert(LegacySessionBytes == 2352, "Keep version-2 session prefix intact");

enum class Result { Ok, Full, NoSample, Busy, Invalid };
enum class RecordPreparation { None, Waiting, GetReady, Practice };

struct Status {
    bool playing = false;
    bool recording = false;
    bool armed = false;
    RecordPreparation preparation = RecordPreparation::None;
    uint8_t countdown = 0;
    uint8_t preparationLoop = 0;
    bool capturing = false;
    bool undo = false;
    uint8_t selected = 0;
    uint8_t step = 0;
    int8_t livePad = -1;
    uint16_t tick = 0;
    uint16_t bpm = 110;
    uint8_t volume = 35;
    uint8_t root = 0;
    uint8_t scale = 0;
    uint8_t swing = 0;
    uint16_t counts[TrackCount]{};
    uint8_t patches[TrackCount]{};
    uint8_t tones[TrackCount]{};
    int8_t pitches[TrackCount]{};
    uint8_t spaces[TrackCount]{};
    bool muted[TrackCount]{};
    uint32_t sampleCount = 0;
    uint32_t captureCount = 0;
    uint32_t revision = 0;
    int16_t peak = 0;
    uint8_t activePads[TrackCount]{};
    uint8_t scene = 3;
    int8_t nextScene = -1;
    bool slices = false;
    bool fxTouched = false;
    bool fxRecording = false;
    bool fxArmed = false;
    bool fxEnabled = false;
    bool fxUndo = false;
    FxPoint effect;
};

class Engine {
public:
    Engine();
    Session session;
    int16_t sample[SampleCapacity]{};
    Result select(int track);
    Result noteOn(int pad);
    void noteOff();
    void togglePlay();
    Result toggleRecord(bool replace = false);
    void stop();
    void setBpm(int bpm);
    void setVolume(int volume);
    void setPatch(int patch);
    Result previewSound(int patch);
    void stopPreview();
    Result queueScene(int scene);
    Result copyScene(int destination);
    static void initializeScenes(Session &session);
    static void upgradeSession(Session &session, uint16_t storedVersion);
    Result setSlices(bool enabled);
    Result touchFx(int x, int y);
    void releaseFx();
    Result recordFx();
    Result enableFx(bool enabled);
    Result clearFx();
    Result undoFx();
    void setTone(int tone);
    void setPitch(int semitones);
    void setSpace(int space);
    void setScale(int scale);
    void setRoot(int root);
    void setSwing(int swing);
    void mute(int track);
    void clearSelected();
    void undo();
    Result loadDemo(int demo = 0);
    Result restoreSong(const Session &song, const int16_t *recording);
    Result startCapture();
    Result audition();
    void capture(const int16_t *frames, size_t count, int stride, int channel);
    void finishCapture();
    void render(int16_t *out, size_t count);
    Status status() const;
    uint32_t revision() const { return revision_; }
    bool idle() const {
        if (playing_ || capturing_ || livePad_ >= 0 || quietSamples_ < SampleRate / 8) return false;
        for (const Voice &voice : voices_) if (voice.active) return false;
        return true;
    }
    static bool valid(const Session &session);
    static int midiNote(int track, int pad, int root, int scale);

private:
    struct Voice {
        bool active = false;
        bool released = false;
        bool live = false;
        bool preview = false;
        uint8_t track = 0;
        uint8_t pad = 0;
        uint8_t patch = 0;
        uint32_t age = 0;
        uint32_t gate = 0;
        uint32_t phase = 0;
        uint32_t phase2 = 0;
        uint32_t increment = 0;
        uint32_t increment2 = 0;
        uint32_t pitchRatio = 65536;
        int32_t envelope = 65536;
        int32_t timbre = 65536;
        int32_t filter = 0;
        int32_t velocity = 100;
        uint32_t samplePosition = 0;
        uint32_t sampleIncrement = 65536;
        uint32_t sampleStart = 0;
        uint32_t sampleEnd = 0;
        bool sliced = false;
        int16_t midi = 60;
        uint16_t level = 4096;
        uint16_t arcadeLevels[3] = {4096, 4096, 4096};
        uint8_t presence = 0;
        uint8_t body1 = 1, body2 = 1;
        const InstrumentSample *instrument = nullptr;
        uint32_t hitProgress = 0;
        uint16_t hitAttack = 64;
        bool looped = false;
        uint8_t resampler = 0;
    };
    Voice voices_[12]{};
    int16_t sine_[1024]{};
    int16_t delay_[4096]{};
    uint32_t noise_ = 0x1234abcd;
    uint16_t delayPosition_ = 0;
    uint8_t selected_ = 0;
    int8_t livePad_ = -1;
    bool playing_ = false;
    bool recording_ = false;
    bool armed_ = false;
    bool capturing_ = false;
    bool undoValid_ = false;
    RecordPreparation preparation_ = RecordPreparation::None;
    uint8_t preparationLoop_ = 0;
    bool preparationStartedPlayback_ = false;
    bool replaceRecording_ = false;
    uint8_t undoTrack_ = 0;
    Track undoTrackData_{};
    uint64_t liveBeatUnits_ = 0;
    uint32_t phase_ = 0;
    uint16_t tick_ = 0;
    uint16_t recordedStart_ = 0;
    uint32_t captureCount_ = 0;
    uint32_t captureSkip_ = 0;
    uint32_t revision_ = 0;
    uint32_t quietSamples_ = SampleRate;
    int16_t peak_ = 0;
    int32_t outputGain_ = 0;
    int32_t highpassInput_ = 0;
    int32_t highpassOutput_ = 0;
    int8_t nextScene_ = -1;
    bool liveSliced_ = false;
    bool liveHeld_ = false;
    bool fxTouched_ = false, fxRecording_ = false, fxArmed_ = false, fxUndoValid_ = false;
    FxPoint touchEffect_;
    FxLoop fxUndo_;
    int32_t fxFilter_ = 0, fxMix_ = 0, fxCutoff_ = 65536, fxEcho_ = 0;
    int16_t fxDelay_[4096]{};
    uint16_t fxDelayPosition_ = 0;
    void trigger(int track, int pad, int velocity, uint32_t gate, bool live,
                 int patch = -1, bool preview = false, bool sliced = false);
    void tune(Voice &voice);
    void processTick();
    void beginPreparation();
    void cancelPreparation();
    void checkpoint();
    int32_t oscillator(uint32_t phase) const;
    int32_t noise();
    int32_t voiceSample(Voice &voice);
    uint16_t quantize(uint16_t tick) const;
    void finishLiveNote();
    void storeScene(int scene);
    void applyScene(int scene);
    void updateFxRecording();
    FxPoint effectPoint() const;
    void finishFxRecording();
};

} // namespace orbit
