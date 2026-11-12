#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Arduino_GFX.h>
#include <databus/Arduino_HWSPI.h>
#include <display/Arduino_ST7789.h>
#ifdef ORBIT_SCREENSHOTS
#include <canvas/Arduino_Canvas.h>
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include "esp_sleep.h"
#include "esp_timer.h"
#include "FreeSansBold10pt7b.h"
#include "board.h"
#include "engine.h"
#include "audio_input.h"
#include "screen_idle.h"
#include "ui_layout.h"
#include "touch_gesture.h"

using namespace orbit;
namespace {
Arduino_DataBus *bus = new Arduino_HWSPI(3, 5, 1, 2);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 4, 0, true, 240, 284);
Engine engine;
SemaphoreHandle_t engineMutex;
TaskHandle_t audioHandle;
constexpr uint16_t BG = 0x0863, PANEL = 0x10c5, BORDER = 0x2969;
constexpr uint16_t WHITE = 0xef7d, DIM = 0x8493, CYAN = 0x4f19;
constexpr uint16_t CORAL = 0xfbaf, AMBER = 0xfdec, PURPLE = 0xbb9f;
const uint16_t accents[] = {CYAN, AMBER, PURPLE, CORAL};
const uint16_t demoAccents[DemoCount] = {
    CORAL, AMBER, PURPLE, CYAN, CORAL, CYAN, AMBER, CYAN, AMBER, CORAL, PURPLE, CYAN
};
const char *trackNames[] = {"MELODY", "BASS", "BEATS", "VOICE"};
const char *drumNames[] = {"KICK", "SNARE", "HAT", "OPEN", "CLAP", "TOM", "SHAKE", "RIM"};
const char *noteNames[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *scaleNames[] = {"DREAMY", "BRIGHT", "MOODY"};
enum Page { Play, Mixer, Sound, Mic, Settings, Confirm, Demos, SoundPicker,
            Menu, Scenes, Slices, LiveFx, SceneCopy, FxOptions };
enum Confirmation { ClearTrack, Demo, RecordTrack, CopySection, ReplaceMoves };
Page page = Play, returnPage = Mixer;
Page menuReturn = Play;
Confirmation confirmation = ClearTrack;
bool dirty = true, storageOk = false, firstRun = false;
bool touchDown = false, suppressTouch = false;
ui::TouchGesture touchGesture;
int heldPad = -1;
uint32_t lastTouchRead = 0, lastPowerRead = 0, lastDraw = 0;
uint32_t lastEdit = 0, savedRevision = 0, lastHealth = 0;
uint32_t recordChoiceUntil = 0;
int recordChoiceSeconds = 3;
int pendingDemo = 0;
int pendingSound = 0, soundFamily = 0, pickerTrack = 0;
int demoPage = 0;
int pendingScene = 0;
uint32_t fullDraws = 0, mixerMoves = 0;
std::atomic<uint32_t> audioMaxUs{0}, audioErrors{0}, renderedBlocks{0};
std::atomic<uint32_t> captureDrops{0};
std::atomic<bool> audioPauseRequested{false}, audioPaused{false};
bool audioStandby = false, cpuSleepFailed = false;
uint32_t lightSleeps = 0;
uint64_t lightSleepUs = 0;
std::atomic<bool> inputProbePending{false};
int16_t inputProbe[64 * InputFrameSamples]{};
std::atomic<bool> speakerMeterPending{false};
struct SpeakerMeter {
    int64_t sum = 0, squares = 0;
    int64_t chunkSums[10]{}, chunkSquares[10]{};
    uint32_t count = 0, skip = 320, clips = 0;
    int peak = 0;
} speakerMeter;
std::atomic<uint32_t> referenceHz{0};
int16_t referenceWave[256]{};
uint32_t touchErrors = 0;
char toastText[48] = "";
uint32_t toastUntil = 0;
Status shown;
board::BatteryStatus battery;
bool batteryDirty = true;
uint32_t lastBatteryRead = 0;
ScreenIdle screenIdle;
bool backlightPending = false;
int16_t micPeaks[48]{};

struct Lock {
    Lock() { xSemaphoreTake(engineMutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(engineMutex); }
};

void toast(const char *text, uint32_t duration = 1800) {
    snprintf(toastText, sizeof(toastText), "%s", text);
    toastUntil = millis() + duration;
    dirty = true;
}

void report(Result result) {
    switch (result) {
    case Result::Full: toast("TRACK FULL - UNDO OR CLEAR"); break;
    case Result::NoSample: toast("RECORD A SOUND FIRST"); break;
    case Result::Busy: toast("FINISH RECORDING FIRST"); break;
    case Result::Invalid: toast("INVALID CONTROL"); break;
    case Result::Ok: break;
    }
}

Status snapshot() { Lock lock; return engine.status(); }

void text(int x, int y, const char *value, uint16_t color = WHITE, int size = 1) {
    gfx->setFont(nullptr);
    gfx->setTextSize(size);
    gfx->setTextColor(color);
    gfx->setCursor(x, y);
    gfx->print(value);
}

void center(int x, int y, int width, const char *value, uint16_t color = WHITE, int size = 1) {
    int pixels = strlen(value) * 6 * size;
    text(x + (width - pixels) / 2, y, value, color, size);
}

void heading(int x, int baseline, const char *value, uint16_t color = WHITE) {
    gfx->setFont(&FreeSansBold10pt7b);
    gfx->setTextSize(1);
    gfx->setTextColor(color);
    gfx->setCursor(x, baseline);
    gfx->print(value);
    gfx->setFont(nullptr);
}

void button(int x, int y, int w, int h, const char *label,
            uint16_t accent = BORDER, bool filled = false) {
    gfx->fillRoundRect(x, y, w, h, 7, filled ? accent : PANEL);
    gfx->drawRoundRect(x, y, w, h, 7, accent);
    center(x, y + (h - 8) / 2, w, label, filled ? BG : WHITE);
}

void button(const ui::Rect &rect, const char *label, uint16_t accent = BORDER, bool filled = false) {
    button(rect.x, rect.y, rect.w, rect.h, label, accent, filled);
}

void switchPage(Page next) {
    { Lock lock; engine.noteOff(); engine.stopPreview(); engine.releaseFx(); }
    heldPad = -1;
    page = next;
    dirty = true;
}

void resumeFromSleep();

bool userActivity() {
    bool woke = screenIdle.activity(millis());
    if (woke) {
        resumeFromSleep();
        dirty = true;
        backlightPending = true;
        Serial.println("SCREEN_WAKE");
    }
    return woke;
}

bool batteryHeaderVisible() {
    return page == Play || page == Mixer || page == Scenes || page == Slices || page == LiveFx ||
        (page == Confirm && confirmation != RecordTrack);
}

void drawBattery() {
    gfx->fillRect(ui::HeaderStatus.x, ui::HeaderStatus.y, ui::HeaderStatus.w, ui::HeaderStatus.h, BG);
    int x = ui::HeaderRight - 71;
    uint16_t color = !battery.valid ? DIM :
        battery.charging ? CYAN : battery.connected && battery.percent >= 0 && battery.percent <= 20 ? CORAL : WHITE;
    gfx->drawRoundRect(x, 12, 23, 13, 2, color);
    gfx->fillRect(x + 23, 16, 2, 5, color);
    if (battery.valid && battery.connected && battery.percent >= 0) {
        int width = battery.percent * 17 / 100;
        if (width) gfx->fillRect(x + 3, 15, width, 7, color);
    } else {
        text(x + 9, 15, battery.valid && !battery.connected ? "X" : "?", DIM);
    }
    char level[8];
    if (!battery.valid) snprintf(level, sizeof(level), "--%%");
    else if (!battery.connected) snprintf(level, sizeof(level), "%s", battery.usb ? "USB" : "NONE");
    else if (battery.percent < 0) snprintf(level, sizeof(level), "--%%");
    else snprintf(level, sizeof(level), "%d%%", battery.percent);
    text(x + 32, 16, level, color);
    if (battery.charging) {
        gfx->fillTriangle(x + 68, 9, x + 61, 19, x + 66, 19, CYAN);
        gfx->fillTriangle(x + 65, 17, x + 71, 17, x + 64, 28, CYAN);
    } else if (battery.valid && battery.usb) {
        gfx->drawFastVLine(x + 63, 11, 5, DIM);
        gfx->drawFastVLine(x + 68, 11, 5, DIM);
        gfx->drawRoundRect(x + 61, 15, 10, 7, 2, DIM);
        gfx->drawFastVLine(x + 66, 22, 5, DIM);
    }
    batteryDirty = false;
}

void pollBattery(uint32_t interval = 2000) {
    uint32_t now = millis();
    if (now - lastBatteryRead < interval) return;
    lastBatteryRead = now;
    board::BatteryStatus next = board::readBattery();
    if (next.valid != battery.valid || next.connected != battery.connected ||
        next.charging != battery.charging || next.usb != battery.usb ||
        next.percent != battery.percent) batteryDirty = true;
    battery = next;
}

void header(const char *title, const Status &, bool back = false) {
    heading(ui::HeaderLeft, 26, title);
    if (!back && page != Confirm && page != Menu)
        gfx->fillTriangle(123, 16, 131, 16, 127, 21, DIM);
    if (back) button(ui::Back, "BACK");
    else drawBattery();
    gfx->drawFastHLine(8, 34, 224, BORDER);
}

void padLabel(int pad, const Status &s, char *out, size_t capacity) {
    if (s.selected == 2) { snprintf(out, capacity, "%s", drumNames[pad]); return; }
    if (s.selected == 3) {
        if (s.slices) { snprintf(out, capacity, "%d", pad + 1); return; }
        int midi = Engine::midiNote(3, pad, 0, s.scale) + s.pitches[3];
        snprintf(out, capacity, "%+d", midi - 60);
        return;
    }
    int midi = Engine::midiNote(s.selected, pad, s.root, s.scale) + s.pitches[s.selected];
    snprintf(out, capacity, "%s", noteNames[midi % 12]);
}

void drawPad(int pad, const Status &s) {
    int row = pad < 4 ? 1 : 0, col = pad % 4;
    int x = 8 + col * 57, y = 81 + row * 62;
    bool active = s.activePads[s.selected] & (1 << pad);
    bool pressed = s.livePad == pad;
    uint16_t accent = accents[s.selected];
    gfx->fillRoundRect(x, y, 53, 56, 7, pressed ? accent : PANEL);
    gfx->drawRoundRect(x, y, 53, 56, 7, active || pressed ? accent : BORDER);
    char label[16];
    padLabel(pad, s, label, sizeof(label));
    int size = s.selected == 2 ? 1 : 2;
    center(x, y + 15, 53, label, pressed ? BG : WHITE, size);
    char detail[12];
    if (s.selected < 2) {
        int midi = Engine::midiNote(s.selected, pad, s.root, s.scale) + s.pitches[s.selected];
        snprintf(detail, sizeof(detail), "%d", midi / 12 - 1);
    } else if (s.selected == 3 && s.slices) snprintf(detail, sizeof(detail), "PIECE");
    else snprintf(detail, sizeof(detail), "%d", pad + 1);
    center(x, y + 40, 53, detail, pressed ? BG : DIM);
}

void preparationText(const Status &s, char *out, size_t capacity) {
    if (s.preparation == RecordPreparation::Waiting)
        snprintf(out, capacity, "PREPARATION STARTS NEXT LOOP");
    else snprintf(out, capacity, "%s %u - LOOP %u/2",
        s.preparation == RecordPreparation::GetReady ? "GET READY" : "PRACTICE",
        s.countdown, s.preparationLoop);
}

void drawProgress(const Status &s) {
    gfx->fillRect(8, 201, 224, 33, BG);
    if (s.preparation == RecordPreparation::GetReady || s.preparation == RecordPreparation::Practice) {
        bool ready = s.preparation == RecordPreparation::GetReady;
        char count[4], detail[36];
        snprintf(count, sizeof(count), "%u", s.countdown);
        center(8, 202, 32, count, ready ? AMBER : CYAN, 3);
        text(48, 202, ready ? "GET READY" : "PRACTICE", ready ? AMBER : CYAN, 2);
        snprintf(detail, sizeof(detail), "LOOP %u/2 - %s", s.preparationLoop,
            ready ? "LISTEN" : "TRY THE PADS");
        text(48, 222, detail, DIM);
        return;
    }
    for (int i = 0; i < 32; ++i) {
        uint16_t color = i == s.step && s.playing ? WHITE :
            (i < s.step && s.playing ? accents[s.selected] : BORDER);
        gfx->fillRect(8 + i * 7, 208, 5, 6, color);
    }
    gfx->fillRect(8, 219, 224, 13, BG);
    if (int32_t(toastUntil - millis()) > 0) {
        center(8, 221, 224, toastText, AMBER);
    } else if (s.armed) {
        char count[36];
        preparationText(s, count, sizeof(count));
        center(8, 221, 224, count, CORAL);
    } else if (s.recording) {
        char rec[40];
        snprintf(rec, sizeof(rec), "RECORDING %s - TAP FINISH", trackNames[s.selected]);
        center(8, 221, 224, rec, CORAL);
    } else {
        center(8, 221, 224, s.muted[s.selected] ? "TRACK OFF - TURN ON IN TRACKS" :
            "BOOT: RECORD   PWR: PLAY/STOP", DIM);
    }
}

void drawTransport(const Status &s) {
    bool rec = s.recording || s.armed;
    button(ui::Footer[0], rec ? (s.armed ? "CANCEL" : "FINISH") : "RECORD", CORAL, rec);
    button(ui::Footer[1], s.playing ? "STOP" : "PLAY", CYAN, s.playing);
    button(ui::Footer[2], "TRACKS", BORDER);
}

void drawPlay(const Status &s) {
    header("ORBIT", s);
    for (int i = 0; i < 4; ++i) {
        bool selected = i == s.selected;
        button(8 + i * 57, 41, 53, 33, trackNames[i], accents[i], selected);
        if (s.muted[i]) center(8 + i * 57, 64, 53, "OFF", selected ? BG : DIM);
        else if (s.counts[i]) gfx->fillCircle(34 + i * 57, 69, 2, selected ? BG : accents[i]);
    }
    for (int i = 0; i < 8; ++i) drawPad(i, s);
    drawProgress(s);
    drawTransport(s);
}

void drawMixerRow(int i, const Status &s) {
    int y = 41 + i * 46;
    gfx->fillRoundRect(8, y, 224, 41, 6, PANEL);
    gfx->drawRoundRect(8, y, 224, 41, 6, s.selected == i ? accents[i] : BORDER);
    text(17, y + 8, trackNames[i], s.muted[i] ? DIM : accents[i]);
    char count[16];
    snprintf(count, sizeof(count), "%u %s", s.counts[i], i < 2 ? "NOTES" : "HITS");
    text(70, y + 8, count, DIM);
    for (int step = 0; step < 16; ++step) {
        bool note = false;
        {
            Lock lock;
            const Track &track = engine.session.tracks[i];
            for (int n = 0; n < track.count; ++n)
                if (track.notes[n].tick / 48 == step) note = true;
        }
        gfx->fillRect(17 + step * 8, y + 26, 6, 6,
            note && !s.muted[i] ? accents[i] : BORDER);
        if (s.playing && s.step / 2 == step)
            gfx->drawFastHLine(17 + step * 8, y + 34, 6, WHITE);
    }
    button(160, y + 5, 65, 31, s.muted[i] ? "OFF" : "ON",
        s.muted[i] ? BORDER : accents[i], !s.muted[i]);
}

void drawMixer(const Status &s) {
    header("TRACKS", s);
    for (int i = 0; i < TrackCount; ++i) drawMixerRow(i, s);
    center(8, 225, 224, "2 BARS / HOLD ROW TO CLEAR", DIM);
    button(ui::Footer[0], "BACK");
    button(ui::Footer[1], "UNDO", s.undo ? AMBER : BORDER);
    button(ui::Footer[2], "SOUND", accents[s.selected]);
}

void updateMixerPlayhead(const Status &previous, const Status &current) {
    int before = previous.step / 2, after = current.step / 2;
    bool moved = previous.playing != current.playing || before != after;
    for (int track = 0; track < TrackCount; ++track) {
        if (previous.counts[track] != current.counts[track] ||
            previous.muted[track] != current.muted[track] ||
            (previous.selected != current.selected &&
                (track == previous.selected || track == current.selected))) {
            drawMixerRow(track, current);
            continue;
        }
        if (!moved) continue;
        int y = 41 + track * 46 + 34;
        if (previous.playing) gfx->drawFastHLine(17 + before * 8, y, 6, PANEL);
        if (current.playing) gfx->drawFastHLine(17 + after * 8, y, 6, WHITE);
    }
    if (moved) ++mixerMoves;
}

void slider(int y, const char *label, int value, uint16_t accent,
            int minimum = 0, int maximum = 100, const char *unit = "") {
    text(12, y, label, DIM);
    char number[8];
    if (minimum < 0) snprintf(number, sizeof(number), "%+d", value);
    else snprintf(number, sizeof(number), "%d%s", value, unit);
    text(minimum < 0 ? 190 : 206, y, number, WHITE);
    int position = (value - minimum) * 208 / (maximum - minimum);
    gfx->fillRoundRect(12, y + 22, 216, 7, 3, BORDER);
    if (minimum < 0) {
        gfx->fillRect(std::min(120, 16 + position), y + 22,
            std::max(1, abs(position - 104)), 7, accent);
        gfx->drawFastVLine(120, y + 18, 15, DIM);
    } else {
        gfx->fillRoundRect(12, y + 22,
            std::max(7, (value - minimum) * 216 / (maximum - minimum)), 7, 3, accent);
    }
    gfx->fillCircle(16 + position, y + 25, 7, accent);
}

void drawSound(const Status &s) {
    header(trackNames[s.selected], s, true);
    if (s.selected == 3) {
        center(8, 55, 224, "YOUR VOICE. YOUR INSTRUMENT.", CORAL);
        button(8, 76, 108, 50, "RECORD SOUND", CORAL);
        button(124, 76, 108, 50, "8 PIECES", CORAL);
    } else if (s.selected == 2) {
        center(8, 55, 224, "8 DRUM SOUNDS - TAP THE PADS", PURPLE);
        center(8, 79, 224, "KICK / SNARE / HATS / CLAP", DIM);
        center(8, 103, 224, "TOM / SHAKER / RIM", DIM);
    } else {
        int patch = s.patches[s.selected];
        center(8, 48, 224, Sounds[patch].name, accents[s.selected], 2);
        center(8, 72, 224, "TAP A HIT / HOLD A LONGER NOTE", DIM);
        button(8, 86, 145, 43, "CHOOSE SOUND", accents[s.selected]);
        button(161, 86, 71, 43, "LISTEN", accents[s.selected]);
    }
    slider(142, "PITCH", s.pitches[s.selected], accents[s.selected], -12, 12);
    center(8, 177, 224, "LOWER     ORIGINAL     HIGHER", DIM);
    slider(188, "ECHO", s.spaces[s.selected], accents[s.selected]);
    button(ui::Footer[0], "VOLUME -");
    char volume[12];
    snprintf(volume, sizeof(volume), "%u%%", s.volume);
    center(78, 247, 84, volume, WHITE, 2);
    button(ui::Footer[2], "VOLUME +");
}

void drawSoundCard(const ui::Rect &rect, int patch, uint16_t accent) {
    bool selected = patch == pendingSound;
    button(rect, "", accent, selected);
    const char *name = Sounds[patch].name;
    const char *space = strchr(name, ' ');
    uint16_t color = selected ? BG : WHITE;
    if (strlen(name) <= 8) center(rect.x, rect.y + 24, rect.w, name, color, 2);
    else if (space) {
        char first[16];
        snprintf(first, sizeof(first), "%.*s", int(space - name), name);
        center(rect.x, rect.y + 12, rect.w, first, color, 2);
        center(rect.x, rect.y + 36, rect.w, space + 1, color, 2);
    } else center(rect.x, rect.y + 26, rect.w, name, color);
}

void drawSoundPicker(const Status &s) {
    header("SOUNDS", s, true);
    button(ui::SoundPrevious, "<", accents[s.selected]);
    button(ui::SoundNext, ">", accents[s.selected]);
    center(53, 47, 134, SoundFamilies[soundFamily], accents[s.selected], 2);
    bool visibleChoice = Sounds[pendingSound].family == soundFamily;
    center(8, 76, 224, "TAP A SOUND TO HEAR IT", DIM);
    for (int i = 0; i < 4; ++i) {
        int patch = familySound(soundFamily, i);
        if (patch >= 0) drawSoundCard(ui::SoundCards[i], patch, accents[s.selected]);
    }
    center(8, 220, 224, "ON THE PADS: TAP OR HOLD", DIM);
    button(ui::FooterPair[0], "CANCEL");
    button(ui::FooterPair[1], visibleChoice ? "USE SOUND" : "PICK A SOUND", accents[s.selected], visibleChoice);
}

void openSoundPicker() {
    Status s = snapshot();
    if (s.selected >= 2) { toast("CHOOSE MELODY OR BASS FIRST"); return; }
    pickerTrack = s.selected;
    pendingSound = s.patches[s.selected];
    soundFamily = Sounds[pendingSound].family;
    toastText[0] = 0;
    switchPage(SoundPicker);
}

void chooseSound(int patch) {
    Result result;
    { Lock lock; result = engine.previewSound(patch); }
    if (result != Result::Ok) {
        if (result == Result::Busy && !snapshot().capturing) toast("LOTS OF NOTES - TRY AGAIN");
        else report(result);
        return;
    }
    pendingSound = patch;
    soundFamily = Sounds[patch].family;
    userActivity();
    dirty = true;
}

void useSound() {
    if (Sounds[pendingSound].family != soundFamily) { toast("TAP A SOUND FIRST"); return; }
    if (snapshot().selected != pickerTrack) {
        toast("TRACK CHANGED - CHOOSE AGAIN");
        switchPage(Sound);
        return;
    }
    { Lock lock; engine.setPatch(pendingSound); }
    switchPage(Sound);
    toast("SOUND CHOSEN");
}

void refreshWaveform(bool fit = false) {
    Lock lock;
    uint32_t count = engine.status().capturing ?
        engine.status().captureCount : engine.session.sampleCount;
    for (int i = 0; i < 48; ++i) {
        int maximum = 0;
        uint32_t width = fit ? count : SampleCapacity;
        uint32_t first = uint32_t(i) * width / 48;
        uint32_t last = std::min(count, uint32_t(i + 1) * width / 48);
        for (uint32_t j = first; j < last; j += 4)
            maximum = std::max(maximum, std::abs(int(engine.sample[j])));
        micPeaks[i] = maximum;
    }
}

void drawMic(const Status &s) {
    header("MIC", s, true);
    center(8, 47, 224, s.capturing ? "RECORDING" : "RECORD UP TO 2 SECONDS", CORAL);
    gfx->drawFastHLine(12, 111, 216, BORDER);
    refreshWaveform();
    for (int i = 0; i < 48; ++i) {
        int height = std::max(1, int(micPeaks[i]) * 32 / 32768);
        gfx->drawFastVLine(14 + i * 4 + i / 6, 111 - height, height * 2, CORAL);
    }
    char duration[32];
    snprintf(duration, sizeof(duration), "%.1f / 2.0 s",
        (s.capturing ? s.captureCount : s.sampleCount) / float(SampleRate));
    center(8, 153, 224, duration, WHITE);
    button(8, 175, 224, 39, s.capturing ? "FINISH RECORDING" :
        (s.sampleCount ? "REPLACE SOUND" : "RECORD SOUND"), CORAL, s.capturing);
    center(8, 220, 224, s.capturing ? "SPEAKER OFF WHILE RECORDING" :
        s.sampleCount ? "REPLACES VOICE SOUND - NO UNDO" : "SING, CLAP OR MAKE A SOUND", DIM);
    button(ui::FooterPair[0], "LISTEN", s.sampleCount && !s.capturing ? CORAL : BORDER);
    button(ui::FooterPair[1], "PLAY PADS", s.sampleCount && !s.capturing ? CYAN : BORDER);
}

void drawSettings(const Status &s) {
    header("SESSION", s, true);
    center(8, 46, 224, "SPEED (BPM) - TAP THE NUMBER", DIM);
    button(8, 66, 53, 43, "-");
    char tempo[12];
    snprintf(tempo, sizeof(tempo), "%u", s.bpm);
    center(63, 76, 114, tempo, CYAN, 3);
    button(179, 66, 53, 43, "+");
    slider(124, "VOLUME", s.volume, AMBER, 0, 80, "%");
    char root[28];
    snprintf(root, sizeof(root), "KEY: %s  (TAP TO CHANGE)", noteNames[s.root]);
    button(8, 165, 224, 29, root, CYAN);
    char mood[24];
    snprintf(mood, sizeof(mood), "MOOD: %s", scaleNames[s.scale]);
    button(8, 202, 224, 29, mood, CYAN);
    button(ui::FooterPair[0], "DEMOS", AMBER);
    button(ui::FooterPair[1], "BACK");
}

void drawDemos(const Status &s) {
    header("DEMOS", s, true);
    for (int card = 0; card < 3; ++card) {
        int i = demoPage * 3 + card;
        if (i >= DemoCount) break;
        int y = 44 + card * 58;
        gfx->fillRoundRect(8, y, 224, 54, 7, PANEL);
        gfx->drawRoundRect(8, y, 224, 54, 7, demoAccents[i]);
        text(18, y + 9, DemoLibrary[i].name, demoAccents[i], 2);
        char detail[40];
        snprintf(detail, sizeof(detail), "%s / %u BPM", DemoLibrary[i].style, DemoLibrary[i].bpm);
        text(18, y + 35, detail, DIM);
    }
    char pages[24];
    snprintf(pages, sizeof(pages), "SONGS %d-%d OF %d", demoPage * 3 + 1,
        std::min(DemoCount, demoPage * 3 + 3), DemoCount);
    center(8, 222, 224, pages, DIM);
    button(ui::Footer[0], "<");
    button(ui::Footer[1], "BACK");
    button(ui::Footer[2], ">");
}

void openDemoPicker() {
    if (snapshot().capturing) { report(Result::Busy); return; }
    toastText[0] = 0;
    switchPage(Demos);
}

void activateDemo(int demo) {
    Result result;
    { Lock lock; result = engine.loadDemo(demo); }
    if (result != Result::Ok) { report(result); return; }
    pendingDemo = demo;
    switchPage(Play);
    char message[48];
    snprintf(message, sizeof(message), "%s - PRESS PLAY", DemoLibrary[demo].name);
    toast(message, 3000);
}

void openPerformance(Page next) {
    Status s = snapshot();
    if (s.capturing) { toast("FINISH YOUR SOUND FIRST"); return; }
    if (next == Slices) {
        if (!s.sampleCount) {
            switchPage(Mic);
            toast("RECORD A SOUND, THEN TRY PIECES", 3000);
            return;
        }
        Result result;
        {
            Lock lock;
            result = engine.select(3);
            if (result == Result::Ok) result = engine.setSlices(true);
        }
        if (result != Result::Ok) { report(result); return; }
    }
    switchPage(next);
}

void drawMenu(const Status &s) {
    header("EXPLORE", s, true);
    const char *names[] = {"PLAY PADS", "TRACKS", "SCENES", "MIC PIECES", "LIVE FX", "SETTINGS"};
    const char *hints[] = {"MAKE A LOOP", "SOUNDS + ON/OFF", "BUILD YOUR SONG",
                          "REMIX YOUR VOICE", "TOUCH THE MUSIC", "SPEED + VOLUME"};
    for (int i = 0; i < 6; ++i) {
        const ui::Rect &r = ui::MenuCards[i];
        button(r, "", accents[i % 4]);
        center(r.x, r.y + 10, r.w, names[i], WHITE);
        center(r.x, r.y + 32, r.w, hints[i], DIM);
    }
    center(8, 222, 224, "TAP A TITLE TO OPEN THIS MENU", DIM);
    button(ui::FooterWide, "BACK TO MUSIC");
}

void drawSceneCards(const Status &s, bool copying) {
    for (int i = 0; i < SceneCount; ++i) {
        const ui::Rect &r = ui::SceneCards[i];
        bool current = i == s.scene;
        button(r, "", accents[i], current && !copying);
        uint16_t color = current && !copying ? BG : WHITE;
        center(r.x, r.y + 9, r.w, SceneNames[i], color, 2);
        center(r.x, r.y + 31, r.w, copying ? "COPY HERE" :
            s.nextScene == i ? "NEXT LOOP" : current ? (s.playing ? "PLAYING" : "CURRENT") : "TAP TO SWITCH",
            color);
        uint8_t mask = 0;
        {
            Lock lock;
            const Track *tracks = current ? engine.session.tracks : engine.session.scenes[i].tracks;
            for (int track = 0; track < TrackCount; ++track)
                if (!tracks[track].muted && tracks[track].count) mask |= 1 << track;
        }
        for (int track = 0; track < TrackCount; ++track)
            gfx->fillRoundRect(r.x + 23 + track * 17, r.y + 48, 12, 9, 2,
                mask & (1 << track) ? (current && !copying ? BG : accents[track]) : BORDER);
    }
}

void drawScenes(const Status &s, bool copying = false) {
    header(copying ? "COPY TO" : "SCENES", s, copying);
    drawSceneCards(s, copying);
    if (copying) {
        center(8, 200, 224, "COPY THIS MUSIC TO A SECTION", DIM);
        center(8, 220, 224, "YOUR CURRENT SECTION IS KEPT", DIM);
        button(ui::FooterWide, "CANCEL");
    } else {
        button(ui::SceneCopy, "COPY CURRENT TO...", CYAN);
        center(8, 224, 224, s.nextScene >= 0 ? "TAP NEXT AGAIN TO CANCEL" :
            "EDITS STAY IN THE CURRENT SECTION", DIM);
        button(ui::Footer[0], "PADS");
        button(ui::Footer[1], s.playing ? "STOP" : "PLAY", CYAN, s.playing);
        button(ui::Footer[2], "MENU");
    }
}

void drawSlicePad(int pad, const Status &s) {
    const ui::Rect r = ui::slicePad(pad);
    bool pressed = s.livePad == pad;
    bool active = s.activePads[3] & (1 << pad);
    button(r, "", active || pressed ? CORAL : BORDER, pressed);
    char name[3];
    snprintf(name, sizeof(name), "%d", pad + 1);
    center(r.x, r.y + 15, r.w, name, pressed ? BG : WHITE, 2);
}

void drawSliceHint(const Status &s) {
    gfx->fillRect(8, 220, 224, 13, BG);
    char preparation[40];
    preparationText(s, preparation, sizeof(preparation));
    center(8, 223, 224, s.recording ? "TAP PIECES - FINISH TO KEEP LOOP" :
        s.armed ? preparation : "TAP PIECES - RECORD MAKES A LOOP", s.armed ? CYAN : DIM);
}

void drawSlices(const Status &s) {
    header("PIECES", s);
    refreshWaveform(true);
    for (int i = 0; i < 48; ++i) {
        int height = std::max(1, int(micPeaks[i]) * 15 / 32768);
        gfx->drawFastVLine(14 + i * 4 + i / 6, 60 - height, height * 2, CORAL);
    }
    for (int i = 0; i <= 8; ++i) gfx->drawFastVLine(8 + i * 28, 43, 34, BORDER);
    button(ui::SliceWhole, "WHOLE SOUND");
    button(ui::SliceCut, "8 PIECES", CORAL, true);
    for (int i = 0; i < 8; ++i) drawSlicePad(i, s);
    drawSliceHint(s);
    drawTransport(s);
}

void drawFxDot(const Status &before, const Status &s) {
    const auto &r = ui::FxPad;
    auto coordinateX = [&](const Status &state) { return r.x + 8 + state.effect.x * (r.w - 17) / 100; };
    auto coordinateY = [&](const Status &state) { return r.y + 8 + (100 - state.effect.y) * (r.h - 17) / 100; };
    if (before.effect.active) {
        int x = coordinateX(before), y = coordinateY(before);
        gfx->fillRect(x - 7, y - 7, 15, 15, PANEL);
        for (int i = 1; i < 4; ++i) {
            int gx = r.x + i * r.w / 4, gy = r.y + i * r.h / 4;
            if (gx >= x - 7 && gx <= x + 7) gfx->drawFastVLine(gx, y - 7, 15, BORDER);
            if (gy >= y - 7 && gy <= y + 7) gfx->drawFastHLine(x - 7, gy, 15, BORDER);
        }
    }
    if (s.effect.active) gfx->fillCircle(coordinateX(s), coordinateY(s), 6, CYAN);
}

void drawFxState(const Status &s) {
    gfx->fillRect(8, 42, 224, 12, BG);
    char preparation[40];
    preparationText(s, preparation, sizeof(preparation));
    center(8, 44, 224, s.fxRecording ? "RECORDING MOVES - ONE LOOP" :
        s.fxArmed ? preparation :
        s.fxEnabled ? (s.playing ? "YOUR RECORDED MOVES ARE PLAYING" : "YOUR RECORDED MOVES ARE READY") :
        "PRESS PLAY, THEN TOUCH + SLIDE", CYAN);
    button(ui::Footer[0], s.fxArmed ? "CANCEL" : s.fxRecording ? "FINISH" : "REC MOVES",
        CORAL, s.fxArmed || s.fxRecording);
    button(ui::Footer[1], s.playing ? "STOP" : "PLAY", CYAN, s.playing);
    button(ui::Footer[2], "OPTIONS");
}

void drawLiveFx(const Status &s) {
    header("LIVE FX", s);
    center(8, 61, 224, "UP = MORE ECHO", DIM);
    const auto &r = ui::FxPad;
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 8, PANEL);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 8, CYAN);
    for (int i = 1; i < 4; ++i) {
        gfx->drawFastVLine(r.x + i * r.w / 4, r.y + 1, r.h - 2, BORDER);
        gfx->drawFastHLine(r.x + 1, r.y + i * r.h / 4, r.w - 2, BORDER);
    }
    text(20, 212, "SOFT", DIM); text(184, 212, "BRIGHT", DIM);
    center(8, 225, 224, "LIFT TO RELEASE - NOTHING ERASED", DIM);
    Status clean;
    drawFxDot(clean, s);
    drawFxState(s);
}

void drawFxOptions(const Status &s) {
    header("FX OPTIONS", s, true);
    button(ui::FxOptions[0], s.fxEnabled ? "RECORDED MOVES: ON" : "RECORDED MOVES: OFF", CYAN);
    button(ui::FxOptions[1], "UNDO / REDO MOVES", s.fxUndo ? AMBER : BORDER);
    button(ui::FxOptions[2], "CLEAR MOVES (UNDO AVAILABLE)", CORAL);
    center(8, 221, 224, "YOUR NOTES AND SOUNDS ARE KEPT", DIM);
    button(ui::FooterWide, "BACK TO LIVE FX");
}

void startMoveRecording() {
    Result result;
    { Lock lock; result = engine.recordFx(); }
    report(result);
    dirty = true;
}

void drawConfirm(const Status &s) {
    if (confirmation == RecordTrack) {
        header("RECORD", s, true);
        heading(18, 77, trackNames[s.selected], accents[s.selected]);
        center(8, 99, 224, "START FRESH OR KEEP YOUR NOTES?", DIM);
        button(8, 123, 224, 52, "START FRESH", CORAL, true);
        center(8, 183, 224, "UNDO CAN RESTORE OLD NOTES", DIM);
        button(8, 202, 224, 43, "KEEP + ADD NOTES", accents[s.selected]);
        char countdown[40];
        snprintf(countdown, sizeof(countdown), "START FRESH IN %d SECONDS", recordChoiceSeconds);
        center(8, 260, 224, countdown, AMBER);
        return;
    }
    header("CONFIRM", s);
    if (confirmation == CopySection || confirmation == ReplaceMoves) {
        heading(18, 83, confirmation == CopySection ? "Copy section?" : "New moves?");
        center(8, 112, 224, confirmation == CopySection ?
            SceneNames[pendingScene] : "REPLACES THIS SECTION'S MOVES", AMBER);
        center(8, 139, 224, confirmation == CopySection ?
            "REPLACES THAT SECTION'S MUSIC" : "UNDO CAN RESTORE OLD MOVES", DIM);
        button(ui::FooterPair[0], "CANCEL");
        button(ui::FooterPair[1], confirmation == CopySection ? "COPY HERE" : "RECORD", CORAL);
        return;
    }
    heading(18, 83, confirmation == Demo ? "Load demo?" : "Clear track?");
    center(8, 112, 224, confirmation == Demo ? "REPLACES MUSIC + RECORDED SOUND" : trackNames[s.selected], AMBER);
    center(8, 139, 224, confirmation == Demo ? DemoLibrary[pendingDemo].name : "UNDO CAN BRING IT BACK", DIM);
    if (confirmation == Demo)
        center(8, 162, 224, DemoLibrary[pendingDemo].style, demoAccents[pendingDemo]);
    button(8, 202, 106, 49, "CANCEL");
    button(126, 202, 106, 49, confirmation == Demo ? "LOAD DEMO" : "CLEAR", CORAL);
}

void draw(const Status &s) {
    ++fullDraws;
    gfx->fillScreen(BG);
    switch (page) {
    case Play: drawPlay(s); break;
    case Mixer: drawMixer(s); break;
    case Sound: drawSound(s); break;
    case Mic: drawMic(s); break;
    case Settings: drawSettings(s); break;
    case Confirm: drawConfirm(s); break;
    case Demos: drawDemos(s); break;
    case SoundPicker: drawSoundPicker(s); break;
    case Menu: drawMenu(s); break;
    case Scenes: drawScenes(s); break;
    case SceneCopy: drawScenes(s, true); break;
    case Slices: drawSlices(s); break;
    case LiveFx: drawLiveFx(s); break;
    case FxOptions: drawFxOptions(s); break;
    }
    if (page != Play && int32_t(toastUntil - millis()) > 0) {
        int y = page == Mixer || page == Demos ? 225 : page == Settings ? 112 :
            page == Confirm ? 181 : page == Mic ? 220 : 224;
        gfx->fillRect(8, y, 224, 10, BG);
        center(8, y + 1, 224, toastText, AMBER);
    }
    shown = s;
    dirty = false;
}

int padAt(int x, int y) {
    if (x < 8 || x >= 232 || y < 81 || y >= 199) return -1;
    int col = (x - 8) / 57, row = (y - 81) / 62;
    if (col > 3 || (x - 8) % 57 >= 53 || (y - 81) % 62 >= 56) return -1;
    return (row == 0 ? 4 : 0) + col;
}

void record() {
    Status s = snapshot();
    if (page == LiveFx || page == FxOptions) {
        if (!s.fxRecording && !s.fxArmed && s.fxEnabled) {
            confirmation = ReplaceMoves;
            returnPage = LiveFx;
            switchPage(Confirm);
        } else startMoveRecording();
        return;
    }
    if (s.fxRecording || s.fxArmed) { toast("FINISH FX MOVES FIRST"); return; }
    if (page == Confirm && confirmation == RecordTrack) {
        switchPage(returnPage);
        return;
    }
    if (page != Mic && !s.recording && !s.armed && !s.capturing) {
        if (s.selected == 3 && !s.sampleCount) {
            report(Result::NoSample);
            return;
        }
        confirmation = RecordTrack;
        returnPage = page == Confirm ? Play : page;
        recordChoiceSeconds = 3;
        recordChoiceUntil = millis() + 3000;
        switchPage(Confirm);
        return;
    }
    Result result;
    {
        Lock lock;
        if (page == Mic) {
            if (!board::micAvailable()) { toast("MIC HARDWARE UNAVAILABLE"); return; }
            if (engine.status().capturing) { engine.finishCapture(); result = Result::Ok; }
            else { board::amplifier(false); result = engine.startCapture(); }
        } else result = engine.toggleRecord();
    }
    report(result);
    dirty = true;
}

void play() {
    if (page == Confirm && confirmation == RecordTrack) switchPage(returnPage);
    { Lock lock; engine.togglePlay(); }
    dirty = true;
}

void beginTrackRecording(bool replace) {
    Result result;
    { Lock lock; result = engine.toggleRecord(replace); }
    switchPage(returnPage == Slices ? Slices : Play);
    report(result);
    if (result == Result::Ok) toast(replace ? "STARTING FRESH" : "ADDING MORE NOTES");
}

void tapTempo() {
    static uint32_t lastTap = 0;
    static uint32_t sum = 0;
    static int intervals = 0;
    uint32_t now = millis(), interval = now - lastTap;
    if (interval >= 333 && interval <= 1000) {
        if (intervals == 4) { sum = 0; intervals = 0; }
        sum += interval;
        ++intervals;
        Lock lock;
        engine.setBpm(60000 * intervals / sum);
    } else { sum = 0; intervals = 0; }
    lastTap = now;
    dirty = true;
}

void handleTap(int x, int y) {
    Status s = snapshot();
    int footer = ui::hit(ui::FooterTouch, x, y);
    int pair = ui::hit(ui::FooterPairTouch, x, y);
    if (page == Confirm) {
        if (confirmation == CopySection || confirmation == ReplaceMoves) {
            if (pair == 0) switchPage(returnPage);
            else if (pair == 1) {
                if (confirmation == CopySection) {
                    Result result;
                    { Lock lock; result = engine.copyScene(pendingScene); }
                    switchPage(Scenes);
                    report(result);
                    if (result == Result::Ok) toast("SECTION COPIED");
                } else {
                    switchPage(LiveFx);
                    startMoveRecording();
                }
            }
            return;
        }
        if (confirmation == RecordTrack) {
            if (ui::BackTouch.contains(x, y)) switchPage(returnPage);
            else if (x >= 8 && x < 232) {
                if (y >= 123 && y <= 175) beginTrackRecording(true);
                else if (y >= 202 && y <= 245) beginTrackRecording(false);
            }
            return;
        }
        if (y >= 202 && y <= 251) {
            if (x >= 126 && confirmation == Demo) {
                activateDemo(pendingDemo);
                return;
            }
            if (x >= 126) { Lock lock; engine.clearSelected(); }
            switchPage(returnPage);
        }
        return;
    }
    if (page != Menu && ui::Navigation.contains(x, y)) {
        if (s.capturing) { toast("FINISH YOUR SOUND FIRST"); return; }
        menuReturn = page;
        switchPage(Menu);
        return;
    }
    if (page == Menu) {
        if (ui::BackTouch.contains(x, y) || ui::FooterWideTouch.contains(x, y)) switchPage(menuReturn);
        else {
            int card = ui::hit(ui::MenuCards, x, y);
            const Page targets[] = {Play, Mixer, Scenes, Slices, LiveFx, Settings};
            if (card >= 0) openPerformance(targets[card]);
        }
        return;
    }
    if (page == SceneCopy) {
        int card = ui::hit(ui::SceneCards, x, y);
        if (ui::BackTouch.contains(x, y) || ui::FooterWideTouch.contains(x, y)) switchPage(Scenes);
        else if (card >= 0) {
            if (card == s.scene) { toast("THIS IS THE CURRENT SECTION"); return; }
            pendingScene = card;
            confirmation = CopySection;
            returnPage = SceneCopy;
            switchPage(Confirm);
        }
        return;
    }
    if (page == Scenes) {
        int card = ui::hit(ui::SceneCards, x, y);
        if (card >= 0) {
            Result result;
            { Lock lock; result = engine.queueScene(card); }
            report(result);
            dirty = true;
        } else if (ui::SceneCopy.contains(x, y)) switchPage(SceneCopy);
        else if (footer == 0) switchPage(Play);
        else if (footer == 1) play();
        else if (footer == 2) { menuReturn = Scenes; switchPage(Menu); }
        return;
    }
    if (page == LiveFx) {
        if (footer == 0) record();
        else if (footer == 1) play();
        else if (footer == 2) switchPage(FxOptions);
        return;
    }
    if (page == FxOptions) {
        if (ui::BackTouch.contains(x, y) || ui::FooterWideTouch.contains(x, y)) switchPage(LiveFx);
        else {
            int control = ui::hit(ui::FxOptions, x, y);
            Result result = Result::Ok;
            {
                Lock lock;
                if (control == 0) result = engine.enableFx(!s.fxEnabled);
                else if (control == 1) result = engine.undoFx();
                else if (control == 2) result = engine.clearFx();
            }
            if (control == 1 && !s.fxUndo) toast("NO MOVES TO UNDO YET");
            else report(result);
            dirty = true;
        }
        return;
    }
    if (page == Slices) {
        if (ui::SliceWhole.contains(x, y)) {
            { Lock lock; report(engine.setSlices(false)); }
            switchPage(Play);
        } else if (footer == 0) record();
        else if (footer == 1) play();
        else if (footer == 2) switchPage(Mixer);
        return;
    }
    if (page != Play && page != Mixer && ui::BackTouch.contains(x, y)) {
        if (s.capturing) { toast("STOP CAPTURE FIRST"); return; }
        switchPage(page == SoundPicker ? Sound : page == Demos ? Settings :
            (page == Settings || page == Mic ? Play : Mixer));
        return;
    }
    if ((page == Play || page == Mixer) && ui::HeaderStatus.contains(x, y)) {
        switchPage(Settings); return;
    }
    if (page == SoundPicker) {
        if (ui::SoundPrevious.contains(x, y) || ui::SoundNext.contains(x, y)) {
            soundFamily = (soundFamily + (x < 120 ? SoundFamilyCount - 1 : 1)) % SoundFamilyCount;
            dirty = true;
        } else if (pair == 0) switchPage(Sound);
        else if (pair == 1) useSound();
        else {
            int card = ui::hit(ui::SoundCards, x, y);
            if (card >= 0) {
                int patch = familySound(soundFamily, card);
                if (patch >= 0) chooseSound(patch);
            }
        }
    } else if (page == Demos) {
        if (x >= 8 && x < 232 && y >= 44 && y < 214 && (y - 44) % 58 < 54) {
            pendingDemo = demoPage * 3 + (y - 44) / 58;
            if (pendingDemo >= DemoCount) return;
            confirmation = Demo;
            returnPage = Demos;
            switchPage(Confirm);
        } else if (footer == 1) switchPage(Settings);
        else if (footer == 0 || footer == 2) {
            int pages = (DemoCount + 2) / 3;
            demoPage = (demoPage + (footer == 0 ? pages - 1 : 1)) % pages;
            dirty = true;
        }
    } else if (page == Play) {
        if (y >= 41 && y < 74) {
            int track = std::min(3, (x - 8) / 57);
            if (track < 0) return;
            Result result;
            { Lock lock; result = engine.select(track); }
            report(result);
            if (result == Result::Ok && track == 3 && !s.sampleCount) switchPage(Mic);
            dirty = true;
        } else if (footer >= 0) {
            if (footer == 0) record();
            else if (footer == 1) play();
            else switchPage(Mixer);
        }
    } else if (page == Mixer) {
        if (y >= 41 && y < 225) {
            int track = (y - 41) / 46;
            if (track >= 4) return;
            Result result = Result::Ok;
            {
                Lock lock;
                if (x >= 158) engine.mute(track);
                else result = engine.select(track);
            }
            report(result);
            dirty = true;
        } else if (footer >= 0) {
            if (footer == 0) switchPage(Play);
            else if (footer == 1) { Lock lock; engine.undo(); dirty = true; }
            else switchPage(Sound);
        }
    } else if (page == Sound) {
        if (y >= 44 && y <= 128) {
            if (s.selected == 3) {
                if (x >= 124) { openPerformance(Slices); return; }
                if (!board::micAvailable()) { toast("MIC HARDWARE UNAVAILABLE"); return; }
                switchPage(Mic);
            } else if (s.selected < 2) {
                if (x >= 161 && y >= 86) {
                    chooseSound(s.patches[s.selected]);
                } else openSoundPicker();
            }
        } else if (footer == 0 || footer == 2) {
            Lock lock;
            engine.setVolume(s.volume + (footer == 0 ? -5 : 5));
            dirty = true;
        }
    } else if (page == Mic) {
        if (y >= 175 && y <= 214) {
            if (!board::micAvailable()) { toast("MIC HARDWARE UNAVAILABLE"); return; }
            {
                Lock lock;
                if (s.capturing) engine.finishCapture();
                else { board::amplifier(false); engine.startCapture(); }
            }
            dirty = true;
        } else if (pair >= 0 && !s.capturing && s.sampleCount) {
            if (pair == 0) {
                Lock lock;
                report(engine.audition());
            } else {
                { Lock lock; engine.select(3); }
                switchPage(Play);
                toast("RECORD + TAP PADS TO MAKE A LOOP", 3500);
            }
        }
    } else if (page == Settings) {
        if (y >= 66 && y <= 109) {
            if (x >= 63 && x <= 177) tapTempo();
            else {
                Lock lock; engine.setBpm(s.bpm + (x < 63 ? -1 : 1)); dirty = true;
            }
        } else if (y >= 165 && y <= 194) {
            Lock lock; engine.setRoot((s.root + 1) % 12); dirty = true;
        } else if (y >= 202 && y <= 231) {
            Lock lock; engine.setScale((s.scale + 1) % 3); dirty = true;
        } else if (pair >= 0) {
            if (pair == 0) {
                openDemoPicker();
            } else switchPage(Play);
        }
    }
}

void handleDrag(int x, int y) {
    Status s = snapshot();
    if (page == Sound && y >= 138 && y <= 224) {
        int value = constrain((x - 12) * 100 / 216, 0, 100);
        Lock lock;
        if (touchGesture.y() < 182)
            engine.setPitch(constrain(((x - 16) * 24 + 104) / 208 - 12, -12, 12));
        else engine.setSpace(value);
        dirty = true;
    } else if (page == Settings && touchGesture.y() >= 120 && touchGesture.y() <= 158) {
        int value = constrain(((x - 16) * 80 + 104) / 208, 0, 80);
        Lock lock; engine.setVolume(value); dirty = true;
    } else if (page == LiveFx && ui::FxPad.contains(touchGesture.x(), touchGesture.y())) {
        if (ui::FxPad.contains(x, y)) {
            Lock lock;
            report(engine.touchFx(constrain((x - ui::FxPad.x) * 100 / (ui::FxPad.w - 1), 0, 100),
                constrain(100 - (y - ui::FxPad.y) * 100 / (ui::FxPad.h - 1), 0, 100)));
        } else { Lock lock; engine.releaseFx(); }
    } else if (page == Play || page == Slices) {
        int pad = page == Slices ? ui::sliceAt(x, y) : padAt(x, y);
        if (pad != heldPad) {
            Result result = Result::Ok;
            {
                Lock lock;
                engine.noteOff();
                if (pad >= 0) result = engine.noteOn(pad);
            }
            heldPad = pad;
            report(result);
        }
    }
}

void pollTouch() {
    uint32_t now = millis();
    if (now - lastTouchRead < 8) return;
    lastTouchRead = now;
    int x = 0, y = 0;
    bool down = false;
    if (!board::readTouch(x, y, down)) {
        ++touchErrors;
        if (touchDown) { Lock lock; engine.noteOff(); engine.releaseFx(); }
        touchDown = false;
        heldPad = -1;
        if (touchErrors % 100 == 1) {
            Serial.println("TOUCH_ERROR read failed");
            toast("TOUCH READ ERROR");
        }
        return;
    }
    if (down && userActivity()) {
        heldPad = -1;
        touchDown = false;
        suppressTouch = true;
        return;
    }
    if (suppressTouch) {
        if (!down) suppressTouch = false;
        return;
    }
    if (down && !touchDown) {
        touchDown = true;
        touchGesture.begin(x, y, now);
        if (page == Confirm && confirmation == RecordTrack) {
            handleTap(x, y);
            suppressTouch = true;
            touchDown = false;
            return;
        }
        if (page == Play || page == Slices) {
            heldPad = page == Slices ? ui::sliceAt(x, y) : padAt(x, y);
            if (heldPad >= 0) {
                Result result;
                { Lock lock; result = engine.noteOn(heldPad); }
                report(result);
            }
        }
        handleDrag(x, y);
    } else if (down && touchDown) {
        touchGesture.move(x, y);
        handleDrag(x, y);
        bool soundHold = page == Play && touchGesture.y() >= 41 && touchGesture.y() < 74;
        bool clearHold = page == Mixer && touchGesture.y() >= 41 &&
            touchGesture.y() < 225 && touchGesture.x() < 158;
        if (touchGesture.takeHold(now, soundHold || clearHold)) {
            if (soundHold) {
                Result result;
                { Lock lock; result = engine.select(std::max(0, std::min(3, (touchGesture.x() - 8) / 57))); }
                report(result);
                if (result == Result::Ok) switchPage(Sound);
                suppressTouch = true;
            } else {
                Result result;
                { Lock lock; result = engine.select((touchGesture.y() - 41) / 46); }
                report(result);
                if (result == Result::Ok) {
                    confirmation = ClearTrack; returnPage = Mixer; switchPage(Confirm);
                }
                suppressTouch = true;
            }
        }
    } else if (!down && touchDown) {
        bool wasPad = heldPad >= 0;
        { Lock lock; engine.noteOff(); engine.releaseFx(); }
        heldPad = -1;
        touchDown = false;
        if (!wasPad && touchGesture.tapped()) handleTap(touchGesture.x(), touchGesture.y());
    }
}

void audioTask(void *) {
    int16_t mono[64];
    int32_t stereo[128];
    int16_t input[64 * InputFrameSamples];
    uint32_t dropStart = 0;
    bool wasCapturing = false;
    uint32_t referencePhase = 0, referenceLeft = 0;
    for (;;) {
        if (audioPauseRequested.load()) {
            audioPaused.store(true);
            while (audioPauseRequested.load()) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            audioPaused.store(false);
            continue;
        }
        uint32_t start = micros();
        bool capturing;
        {
            Lock lock;
            capturing = engine.status().capturing;
            engine.render(mono, 64);
        }
        if (capturing && !wasCapturing) {
            dropStart = board::rxDrops();
            captureDrops.store(0);
        }
        audioMaxUs.store(std::max(audioMaxUs.load(), micros() - start));
        uint32_t hz = referenceHz.load();
        if (hz) {
            if (!referenceLeft) referenceLeft = SampleRate / 2;
            uint32_t increment = uint64_t(hz) * 4294967296ull / SampleRate;
            int volume;
            { Lock lock; volume = engine.session.volume; }
            for (int i = 0; i < 64; ++i) {
                uint32_t fade = std::min<uint32_t>(64, std::min(referenceLeft,
                    SampleRate / 2 - referenceLeft));
                mono[i] = int32_t(referenceWave[referencePhase >> 24]) * volume / 256 * fade / 64;
                referencePhase += increment;
                if (!--referenceLeft) {
                    std::fill(mono + i + 1, mono + 64, 0);
                    referencePhase = 0;
                    referenceHz.store(0);
                    break;
                }
            }
        }
        for (int i = 0; i < 64; ++i) stereo[i * 2] = stereo[i * 2 + 1] = int32_t(mono[i]) * 65536;
        size_t written = 0;
        esp_err_t result = i2s_channel_write(board::tx, stereo, sizeof(stereo), &written, 100);
        if (result != ESP_OK || written != sizeof(stereo)) {
            ++audioErrors;
            Serial.printf("AUDIO_WRITE_ERROR %s bytes=%u\n", esp_err_to_name(result), unsigned(written));
        }
        size_t bytes = 0;
        result = i2s_channel_read(board::rx, input, sizeof(input), &bytes, 20);
        if (result != ESP_OK || bytes != sizeof(input)) {
            ++audioErrors;
            Serial.printf("AUDIO_READ_ERROR %s bytes=%u\n", esp_err_to_name(result), unsigned(bytes));
        }
        if (inputProbePending.load() && bytes == sizeof(input)) {
            std::memcpy(inputProbe, input, sizeof(input));
            inputProbePending.store(false);
        }
        if (speakerMeterPending.load() && bytes == sizeof(input)) {
            for (int i = 0; i < 64 && speakerMeter.count < 6400; ++i) {
                if (speakerMeter.skip) { --speakerMeter.skip; continue; }
                int value = input[i * InputFrameSamples + InputMicChannel];
                speakerMeter.sum += value;
                speakerMeter.squares += int64_t(value) * value;
                speakerMeter.chunkSums[speakerMeter.count / 640] += value;
                speakerMeter.chunkSquares[speakerMeter.count / 640] += int64_t(value) * value;
                speakerMeter.peak = std::max(speakerMeter.peak, abs(value));
                speakerMeter.clips += abs(value) >= 32760;
                ++speakerMeter.count;
            }
            if (speakerMeter.count == 6400) speakerMeterPending.store(false);
        }
        if (capturing && bytes) {
            uint32_t drops = board::rxDrops() - dropStart;
            Lock lock;
            engine.capture(input, bytes / InputFrameBytes, InputFrameSamples, InputMicChannel);
            captureDrops.store(drops);
        }
        wasCapturing = capturing;
        ++renderedBlocks;
    }
}

void resumeFromSleep() {
    if (!audioStandby) return;
    if (!board::resumeAudio()) {
        gfx->displayOn();
        gfx->fillScreen(BG);
        heading(24, 90, "Wake error", CORAL);
        text(24, 120, "PLEASE RESTART ORBIT");
        digitalWrite(6, HIGH);
        Serial.printf("FATAL sleep restore: %s\n", board::error());
        while (true) delay(1000);
    }
    audioPauseRequested.store(false);
    xTaskNotifyGive(audioHandle);
    audioStandby = false;
    gfx->displayOn();
    board::amplifier(true);
}

bool enterSleep() {
    esp_err_t result = esp_sleep_enable_timer_wakeup(250000);
    if (result != ESP_OK) {
        Serial.printf("SLEEP_ERROR timer wake: %s\n", esp_err_to_name(result));
        return false;
    }
    audioPauseRequested.store(true);
    uint32_t start = millis();
    while (!audioPaused.load() && millis() - start < 250) delay(1);
    if (!audioPaused.load()) {
        audioPauseRequested.store(false);
        xTaskNotifyGive(audioHandle);
        Serial.println("SLEEP_ERROR audio pause timed out");
        return false;
    }
    digitalWrite(6, LOW);
    backlightPending = false;
    audioStandby = true;
    if (!board::suspendAudio()) {
        Serial.println("SLEEP_ERROR peripheral standby");
        resumeFromSleep();
        return false;
    }
    gfx->displayOff();
    cpuSleepFailed = false;
    Serial.println("SCREEN_SLEEP audio=standby");
    return true;
}

uint32_t checksum(const uint8_t *data, size_t size, uint32_t hash = 2166136261u) {
    for (size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}

bool loadSession() {
    if (!LittleFS.exists("/orbit.bin")) return false;
    File file = LittleFS.open("/orbit.bin", "r");
    static Session loaded;
    uint32_t storedHash = 0;
    constexpr size_t headerBytes = offsetof(Session, tracks);
    if (!file || file.read(reinterpret_cast<uint8_t *>(&loaded), headerBytes) != headerBytes ||
        (loaded.version < 1 || loaded.version > 5) || loaded.sampleCount > SampleCapacity) {
        Serial.println("SAVE_ERROR invalid session");
        toast("SAVE INVALID - DEMO LOADED", 4000);
        return false;
    }
    uint16_t storedVersion = loaded.version;
    bool legacy = storedVersion == 1;
    size_t trackBytes = legacy ? offsetof(Track, pitch) : sizeof(Track);
    size_t extensionBytes = storedVersion >= 3 ? sizeof(Session) - LegacySessionBytes : 0;
    if (file.size() != headerBytes + TrackCount * trackBytes + extensionBytes +
        loaded.sampleCount * 2 + sizeof(storedHash)) {
        Serial.println("SAVE_ERROR invalid session size");
        toast("SAVE INVALID - DEMO LOADED", 4000);
        return false;
    }
    uint32_t hash = checksum(reinterpret_cast<const uint8_t *>(&loaded), headerBytes);
    for (Track &track : loaded.tracks) {
        if (file.read(reinterpret_cast<uint8_t *>(&track), trackBytes) != trackBytes) {
            Serial.println("SAVE_ERROR short track read"); return false;
        }
        hash = checksum(reinterpret_cast<const uint8_t *>(&track), trackBytes, hash);
    }
    if (extensionBytes) {
        auto *extension = reinterpret_cast<uint8_t *>(&loaded) + LegacySessionBytes;
        if (file.read(extension, extensionBytes) != extensionBytes) {
            Serial.println("SAVE_ERROR short performance read"); return false;
        }
        hash = checksum(extension, extensionBytes, hash);
    }
    Engine::upgradeSession(loaded, storedVersion);
    if (!Engine::valid(loaded)) {
        Serial.println("SAVE_ERROR invalid track settings");
        toast("SAVE INVALID - DEMO LOADED", 4000);
        return false;
    }
    size_t bytes = loaded.sampleCount * 2;
    if (file.read(reinterpret_cast<uint8_t *>(engine.sample), bytes) != bytes ||
        file.read(reinterpret_cast<uint8_t *>(&storedHash), sizeof(storedHash)) != sizeof(storedHash)) {
        Serial.println("SAVE_ERROR short read"); return false;
    }
    hash = checksum(reinterpret_cast<const uint8_t *>(engine.sample), bytes, hash);
    if (hash != storedHash) { Serial.println("SAVE_ERROR checksum"); return false; }
    engine.session = loaded;
    Serial.printf("SESSION_LOADED sample=%u legacy=%d\n", loaded.sampleCount, legacy);
    return true;
}

void saveSession() {
    if (!storageOk) return;
    // Audio is stopped before flash writes; the audio task only renders silence.
    static Session copy;
    { Lock lock; if (!engine.idle()) return; copy = engine.session; }
    File file = LittleFS.open("/orbit.tmp", "w");
    size_t bytes = copy.sampleCount * 2;
    uint32_t hash = checksum(reinterpret_cast<const uint8_t *>(&copy), sizeof(copy));
    hash = checksum(reinterpret_cast<const uint8_t *>(engine.sample), bytes, hash);
    bool ok = file &&
        file.write(reinterpret_cast<const uint8_t *>(&copy), sizeof(copy)) == sizeof(copy) &&
        file.write(reinterpret_cast<const uint8_t *>(engine.sample), bytes) == bytes &&
        file.write(reinterpret_cast<const uint8_t *>(&hash), sizeof(hash)) == sizeof(hash);
    file.flush();
    file.close();
    if (ok) ok = LittleFS.rename("/orbit.tmp", "/orbit.bin");
    if (!ok) {
        Serial.println("SAVE_ERROR write/rename");
        toast("SAVE FAILED - CHECK STORAGE", 4000);
        lastEdit = millis();
        return;
    }
    savedRevision = engine.revision();
    Serial.printf("SESSION_SAVED revision=%u bytes=%u\n", savedRevision, unsigned(sizeof(copy) + bytes));
    toast("SESSION SAVED", 1000);
}

void pollButtons() {
    static bool bootDown = false, bootHeld = false, bootWakeOnly = false;
    static uint32_t bootStart = 0, changed = 0;
    uint32_t now = millis();
    bool down = digitalRead(9) == LOW;
    if (down != bootDown && now - changed > 30) {
        changed = now;
        bootDown = down;
        if (down) {
            bootWakeOnly = userActivity();
            bootStart = now;
            bootHeld = false;
        } else {
            userActivity();
            if (!bootWakeOnly && !bootHeld && now - bootStart >= 30) record();
            bootWakeOnly = false;
        }
    }
    if (bootDown) userActivity();
    if (bootDown && !bootWakeOnly && !bootHeld && now - bootStart > 700) {
        bootHeld = true;
        if (page == Confirm && confirmation == RecordTrack) switchPage(returnPage);
        if (page == LiveFx || page == FxOptions) {
            Lock lock; report(engine.undoFx());
        } else {
            { Lock lock; engine.undo(); }
            toast("UNDO / REDO");
        }
    }
    if (now - lastPowerRead > 50) {
        lastPowerRead = now;
        if (board::powerPressed() && !userActivity()) play();
    }
}

void pollSleeping() {
    pollButtons();
    // PWR has no MCU interrupt; a short timer wake also notices USB being plugged in.
    pollBattery(250);
    if (!screenIdle.sleeping()) return;
    if (board::touchWakePending()) {
        userActivity();
        suppressTouch = true;
        return;
    }
    if (battery.valid && !battery.usb && !cpuSleepFailed) {
        int64_t start = esp_timer_get_time();
        esp_err_t result = board::lightSleep();
        if (result == ESP_OK) {
            ++lightSleeps;
            lightSleepUs += uint64_t(esp_timer_get_time() - start);
        }
        if (result == ESP_ERR_SLEEP_REJECT ||
            (result == ESP_OK && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO)) {
            pollButtons();
            if (screenIdle.sleeping()) {
                userActivity();
                suppressTouch = true;
            }
        } else if (result != ESP_OK) {
            Serial.printf("SLEEP_ERROR light sleep: %s\n", esp_err_to_name(result));
            cpuSleepFailed = true;
            userActivity();
            toast("LOW POWER ERROR - RESTART", 4000);
        }
    } else delay(20);
}

void serialControl() {
    static char command[48];
    static size_t length = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            command[length] = 0;
            length = 0;
            if (strcmp(command, "status") && strcmp(command, "fingerprint")) userActivity();
            if (!strcmp(command, "screenshot")) {
#ifdef ORBIT_SCREENSHOTS
                Arduino_Canvas canvas(board::Width, board::Height, nullptr);
                if (!canvas.begin()) {
                    Serial.println("SCREENSHOT_ERROR framebuffer allocation");
                    continue;
                }
                Arduino_GFX *display = gfx;
                gfx = &canvas;
                gfx->setTextWrap(false);
                draw(snapshot());
                gfx = display;
                dirty = true;
                const auto *pixels = reinterpret_cast<const uint8_t *>(canvas.getFramebuffer());
                constexpr size_t bytes = board::Width * board::Height * 2;
                Serial.printf("SCREENSHOT_BEGIN width=%d height=%d bytes=%u fnv=%08lx\n",
                    board::Width, board::Height, unsigned(bytes),
                    (unsigned long)checksum(pixels, bytes));
                size_t written = 0;
                uint32_t lastProgress = millis();
                while (written < bytes && uint32_t(millis() - lastProgress) < 5000) {
                    size_t count = Serial.write(pixels + written, std::min<size_t>(512, bytes - written));
                    written += count;
                    if (count) lastProgress = millis();
                    delay(1);
                }
                Serial.println(written == bytes ? "\nSCREENSHOT_END" : "\nSCREENSHOT_ERROR short write");
#else
                Serial.println("COMMAND_ERROR build with --screenshots to enable capture");
#endif
            } else if (!strcmp(command, "status")) {
                Status s = snapshot();
                Serial.printf("ORBIT_STATUS play=%d rec=%d armed=%d step=%u selected=%u bpm=%u root=%u scale=%u "
                    "notes=%u,%u,%u,%u sample=%u heap=%u max_render_us=%u audio_errors=%u blocks=%u "
                    "rx_drops=%u capture_drops=%u peak=%d muted=%d,%d,%d,%d full_draws=%u mixer_moves=%u "
                    "pitch=%d,%d,%d,%d tone=%u,%u,%u,%u echo=%u,%u,%u,%u page=%d volume=%u "
                    "battery=%d battery_present=%d charging=%d usb=%d battery_valid=%d "
                    "screen_sleep=%d idle_ms=%u patch=%u,%u,%u,%u sound_choice=%d sound_family=%d "
                    "scene=%u next_scene=%d slices=%d fx_rec=%d fx_armed=%d fx_on=%d fx_touch=%d "
                    "fx_x=%u fx_y=%u fx_active=%d capture=%d bank=sampled-v2 "
                    "preparation=%d countdown=%u preparation_loop=%u "
                    "audio_sleep=%d light_sleeps=%u light_sleep_us=%llu sleep_error=%d\n",
                    s.playing, s.recording, s.armed, s.step, s.selected, s.bpm, s.root, s.scale,
                    s.counts[0], s.counts[1], s.counts[2], s.counts[3], s.sampleCount,
                    ESP.getFreeHeap(), audioMaxUs.load(), audioErrors.load(), renderedBlocks.load(),
                    board::rxDrops(), captureDrops.load(), s.peak,
                    s.muted[0], s.muted[1], s.muted[2], s.muted[3], fullDraws, mixerMoves,
                    s.pitches[0], s.pitches[1], s.pitches[2], s.pitches[3],
                    s.tones[0], s.tones[1], s.tones[2], s.tones[3],
                    s.spaces[0], s.spaces[1], s.spaces[2], s.spaces[3], int(page), s.volume,
                    battery.percent, battery.connected, battery.charging, battery.usb, battery.valid,
                    screenIdle.sleeping(), unsigned(screenIdle.idleMs(millis())),
                    s.patches[0], s.patches[1], s.patches[2], s.patches[3], pendingSound + 1, soundFamily + 1,
                    s.scene + 1, s.nextScene < 0 ? 0 : s.nextScene + 1, s.slices,
                    s.fxRecording, s.fxArmed, s.fxEnabled, s.fxTouched, s.effect.x, s.effect.y, s.effect.active,
                    s.capturing, int(s.preparation), s.countdown, s.preparationLoop,
                    audioStandby, lightSleeps, (unsigned long long)lightSleepUs, cpuSleepFailed);
            } else if (!strcmp(command, "sample-stats")) {
                uint32_t count;
                bool capturing;
                {
                    Lock lock;
                    capturing = engine.status().capturing;
                    count = engine.session.sampleCount;
                }
                if (capturing) {
                    Serial.println("COMMAND_ERROR stop capture before sample-stats");
                    continue;
                }
                // Only this UI task can start a new capture, so the sample stays
                // stable here without holding the audio mutex during the scan.
                int peak = 0;
                int64_t squares = 0, changes = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    int value = engine.sample[i];
                    peak = std::max(peak, abs(value));
                    squares += int64_t(value) * value;
                    if (i) changes += abs(value - engine.sample[i - 1]);
                }
                Serial.printf("SAMPLE_STATS count=%u peak=%d rms=%.1f mean_delta=%.1f\n",
                    count, peak, count ? sqrt(double(squares) / count) : 0,
                    count > 1 ? double(changes) / (count - 1) : 0);
            } else if (!strncmp(command, "speaker-tone ", 13)) {
                char *end = nullptr;
                long hz = strtol(command + 13, &end, 10);
                bool idle;
                { Lock lock; idle = engine.idle(); }
                if (end == command + 13 || *end || hz < 50 || hz > 6000)
                    Serial.println("SPEAKER_TONE_ERROR use speaker-tone 50..6000 Hz");
                else if (!idle || referenceHz.load())
                    Serial.println("SPEAKER_TONE_ERROR stop all sounds and recording first");
                else {
                    for (int i = 0; i < 256; ++i)
                        referenceWave[i] = std::sin(i * 6.283185307179586 / 256) * 32767;
                    referenceHz.store(hz);
                }
            } else if (!strcmp(command, "speaker-level")) {
                Status s = snapshot();
                if (s.capturing || s.recording || s.armed || s.fxRecording || s.fxArmed ||
                    speakerMeterPending.load()) {
                    Serial.println("SPEAKER_LEVEL_ERROR finish recording or measurement first");
                    continue;
                }
                speakerMeter = SpeakerMeter{};
                speakerMeterPending.store(true);
                uint32_t started = millis();
                while (speakerMeterPending.load() && millis() - started < 1000) delay(1);
                if (speakerMeterPending.load()) {
                    Serial.println("SPEAKER_LEVEL_ERROR no complete input measurement");
                    continue;
                }
                double mean = double(speakerMeter.sum) / speakerMeter.count;
                double variance = double(speakerMeter.squares) / speakerMeter.count - mean * mean;
                double noteVariance = 0;
                for (int window = 0; window < 9; ++window) {
                    double mean = double(speakerMeter.chunkSums[window] + speakerMeter.chunkSums[window + 1]) / 1280;
                    double squares = double(speakerMeter.chunkSquares[window] + speakerMeter.chunkSquares[window + 1]) / 1280;
                    noteVariance = std::max(noteVariance, squares - mean * mean);
                }
                Serial.printf("SPEAKER_LEVEL frames=%u rms=%.2f note_rms=%.2f peak=%d clips=%u\n",
                    speakerMeter.count, sqrt(std::max(0.0, variance)), sqrt(noteVariance),
                    speakerMeter.peak, speakerMeter.clips);
            } else if (!strcmp(command, "input-probe")) {
                inputProbePending.store(true);
                uint32_t started = millis();
                while (inputProbePending.load() && millis() - started < 1000) delay(1);
                if (inputProbePending.load()) {
                    Serial.println("INPUT_PROBE_ERROR no input block");
                    continue;
                }
                for (int i = 0; i < 64; ++i)
                    Serial.printf("INPUT %d %d %d %d\n", inputProbe[i * 4],
                        inputProbe[i * 4 + 1], inputProbe[i * 4 + 2], inputProbe[i * 4 + 3]);
            } else if (!strcmp(command, "sample-dump")) {
                Status s = snapshot();
                if (s.capturing || s.playing) {
                    Serial.println("COMMAND_ERROR stop playback and capture before sample-dump");
                    continue;
                }
                Serial.printf("SAMPLE_DATA count=%u\n", s.sampleCount);
                Serial.write(reinterpret_cast<const uint8_t *>(engine.sample), s.sampleCount * sizeof(int16_t));
                Serial.println("\nSAMPLE_END");
            } else if (!strcmp(command, "reboot")) ESP.restart();
            else if (!strcmp(command, "wake")) userActivity();
            else if (!strcmp(command, "menu")) { menuReturn = page; openPerformance(Menu); }
            else if (!strcmp(command, "scenes")) openPerformance(Scenes);
            else if (!strcmp(command, "slices")) openPerformance(Slices);
            else if (!strcmp(command, "fx")) openPerformance(LiveFx);
            else if (!strcmp(command, "fx-release")) { Lock lock; engine.releaseFx(); }
            else if (!strcmp(command, "fx-record")) {
                Result result;
                { Lock lock; result = engine.recordFx(); }
                Serial.printf("FX_RESULT result=%d\n", int(result));
                report(result); dirty = true;
            } else if (!strcmp(command, "fx-undo") || !strcmp(command, "fx-clear") ||
                       !strcmp(command, "fx-toggle")) {
                Result result;
                {
                    Lock lock;
                    result = !strcmp(command, "fx-undo") ? engine.undoFx() :
                        !strcmp(command, "fx-clear") ? engine.clearFx() :
                        engine.enableFx(!engine.status().fxEnabled);
                }
                Serial.printf("FX_RESULT result=%d\n", int(result));
                report(result); dirty = true;
            } else if (!strncmp(command, "fx ", 3)) {
                int x, y; char extra;
                if (sscanf(command + 3, "%d %d %c", &x, &y, &extra) != 2)
                    Serial.println("COMMAND_ERROR use fx X Y (0..100)");
                else {
                    Result result;
                    { Lock lock; result = engine.touchFx(x, y); }
                    Serial.printf("FX_RESULT result=%d\n", int(result));
                    report(result);
                }
            } else if (!strncmp(command, "scene ", 6) || !strncmp(command, "copy-scene ", 11)) {
                bool copying = !strncmp(command, "copy-scene ", 11);
                const char *argument = command + (copying ? 11 : 6);
                char *end = nullptr;
                long scene = strtol(argument, &end, 10);
                if (end == argument || *end || scene < 1 || scene > SceneCount)
                    Serial.println("COMMAND_ERROR use scene/copy-scene 1..4");
                else {
                    Result result;
                    {
                        Lock lock;
                        result = copying ? engine.copyScene(int(scene - 1)) : engine.queueScene(int(scene - 1));
                    }
                    Serial.printf("SCENE_RESULT result=%d\n", int(result));
                    report(result); dirty = true;
                }
            } else if (!strcmp(command, "whole")) {
                Result result;
                { Lock lock; result = engine.setSlices(false); }
                report(result);
                if (result == Result::Ok) switchPage(Play);
            } else if (!strcmp(command, "fingerprint")) {
                Status s = snapshot();
                if (s.playing || s.capturing) {
                    Serial.println("COMMAND_ERROR stop playback/capture before fingerprint");
                    continue;
                }
                uint32_t legacy = 2166136261u;
                const uint8_t zero = 0;
                for (const Track &track : engine.session.tracks) {
                    legacy = checksum(reinterpret_cast<const uint8_t *>(&track), offsetof(Track, reserved), legacy);
                    legacy = checksum(&zero, 1, legacy);
                }
                Serial.printf("SESSION_FINGERPRINT tracks=%08lx sample=%08lx fx=%08lx scene=%u legacy_tracks=%08lx scenes=%08lx\n",
                    (unsigned long)checksum(reinterpret_cast<const uint8_t *>(engine.session.tracks),
                        sizeof(engine.session.tracks)),
                    (unsigned long)checksum(reinterpret_cast<const uint8_t *>(engine.sample),
                        engine.session.sampleCount * 2),
                    (unsigned long)checksum(reinterpret_cast<const uint8_t *>(&engine.session.fx),
                        sizeof(engine.session.fx)), engine.session.activeScene + 1, (unsigned long)legacy,
                    (unsigned long)checksum(reinterpret_cast<const uint8_t *>(engine.session.scenes),
                        sizeof(engine.session.scenes)));
            } else if (!strncmp(command, "echo ", 5)) {
                char *end = nullptr;
                long echo = strtol(command + 5, &end, 10);
                if (end == command + 5 || *end || echo < 0 || echo > 100)
                    Serial.println("COMMAND_ERROR use echo 0..100");
                else { Lock lock; engine.setSpace(int(echo)); dirty = true; }
            } else if (!strncmp(command, "tap ", 4)) {
                int x, y; char extra;
                if (sscanf(command + 4, "%d %d %c", &x, &y, &extra) != 2 ||
                    x < 0 || x >= 240 || y < 0 || y >= 284)
                    Serial.println("COMMAND_ERROR use tap X Y within display");
                else { userActivity(); handleTap(x, y); }
            }
            else if (!strcmp(command, "sounds")) {
                userActivity();
                openSoundPicker();
                for (int i = 0; i < SoundCount; ++i)
                    Serial.printf("SOUND %d %s / %s\n", i + 1, Sounds[i].name,
                        SoundFamilies[Sounds[i].family]);
            } else if (!strncmp(command, "preview ", 8)) {
                char *end = nullptr;
                long sound = strtol(command + 8, &end, 10);
                if (end == command + 8 || *end || sound < 1 || sound > SoundCount)
                    Serial.println("COMMAND_ERROR use preview 1..30");
                else if (snapshot().selected >= 2)
                    Serial.println("COMMAND_ERROR choose track 0 or 1 first");
                else {
                    if (page != SoundPicker) openSoundPicker();
                    chooseSound(int(sound - 1));
                }
            } else if (!strcmp(command, "use-sound") || !strcmp(command, "cancel-sound")) {
                if (page != SoundPicker) Serial.println("COMMAND_ERROR open sounds first");
                else if (!strcmp(command, "use-sound")) useSound();
                else switchPage(Sound);
            }
            else if (!strcmp(command, "demos")) {
                openDemoPicker();
                for (int i = 0; i < DemoCount; ++i)
                    Serial.printf("DEMO %d %s / %s / %u BPM\n", i + 1,
                        DemoLibrary[i].name, DemoLibrary[i].style, DemoLibrary[i].bpm);
            }
            else if (!strcmp(command, "demo") || !strncmp(command, "demo ", 5)) {
                if (!strcmp(command, "demo")) activateDemo(0);
                else {
                    char *end = nullptr;
                    long number = strtol(command + 5, &end, 10);
                    if (end == command + 5 || *end || number < 1 || number > DemoCount)
                        Serial.println("COMMAND_ERROR use demo 1..12");
                    else activateDemo(int(number - 1));
                }
            }
            else if (!strcmp(command, "play")) play();
            else if (!strcmp(command, "record")) record();
            else if (!strcmp(command, "stop")) {
                if (page == Confirm && confirmation == RecordTrack) switchPage(returnPage);
                Lock lock; engine.stop(); dirty = true;
            }
            else if (!strcmp(command, "release")) { Lock lock; engine.noteOff(); }
            else if (!strcmp(command, "mic")) {
                board::amplifier(false);
                Lock lock; engine.startCapture();
            } else if (!strncmp(command, "note ", 5)) {
                Result result;
                { Lock lock; result = engine.noteOn(atoi(command + 5)); }
                report(result);
            } else if (!strncmp(command, "track ", 6)) {
                Result result;
                { Lock lock; result = engine.select(atoi(command + 6)); }
                report(result); dirty = true;
            } else if (!strncmp(command, "pitch ", 6)) {
                char *end = nullptr;
                long pitch = strtol(command + 6, &end, 10);
                if (end == command + 6 || *end || pitch < -12 || pitch > 12)
                    Serial.println("COMMAND_ERROR pitch must be -12..12 semitones");
                else {
                    Lock lock; engine.setPitch(int(pitch)); dirty = true;
                }
            } else if (!strncmp(command, "volume ", 7)) {
                char *end = nullptr;
                long volume = strtol(command + 7, &end, 10);
                if (end == command + 7 || *end || volume < 0 || volume > 80)
                    Serial.println("COMMAND_ERROR volume must be 0..80 percent");
                else {
                    Lock lock; engine.setVolume(int(volume)); dirty = true;
                }
            } else if (!strncmp(command, "page ", 5)) {
                int next = atoi(command + 5);
                if ((next >= Play && next <= Settings) || (next >= Menu && next <= FxOptions))
                    openPerformance(Page(next));
            } else Serial.println("COMMAND_ERROR use status/play/stop/record/note N/release/track N/pitch N/volume N/mic/page N/sounds/preview N/use-sound/cancel-sound/demos/demo 1..12/wake/sample-stats/sample-dump/input-probe/reboot");
        } else if (length + 1 < sizeof(command)) command[length++] = c;
        else { length = 0; Serial.println("COMMAND_ERROR too long"); }
    }
}
}

void setup() {
    bool serialBufferOk = Serial.setTxBufferSize(2048) == 2048;
    Serial.begin(115200);
    Serial.setTxTimeoutMs(100);
    if (!serialBufferOk) Serial.println("SERIAL_ERROR diagnostic buffer allocation failed");
    engineMutex = xSemaphoreCreateMutex();
    if (!engineMutex) { Serial.println("FATAL mutex allocation"); while (true) delay(1000); }
    bool hardware = board::begin();
    if (!gfx->begin(40000000)) { Serial.println("FATAL display"); while (true) delay(1000); }
    pinMode(6, OUTPUT); digitalWrite(6, HIGH);
    gfx->setTextWrap(false);
    gfx->fillScreen(BG);
    if (!hardware) {
        heading(20, 90, "Hardware error", CORAL);
        text(15, 120, board::error(), WHITE);
        Serial.printf("FATAL %s\n", board::error());
        while (true) delay(1000);
    }
    battery = board::readBattery();
    lastBatteryRead = millis();
    storageOk = LittleFS.begin(true);
    if (!storageOk) {
        Serial.println("STORAGE_ERROR cannot mount");
        toast("STORAGE ERROR - NOT SAVING", 5000);
    }
    if (!storageOk || !loadSession()) {
        engine.loadDemo();
        firstRun = true;
    }
    savedRevision = engine.revision();
    if (xTaskCreate(audioTask, "orbit-audio", 6144, nullptr, 4, &audioHandle) != pdPASS) {
        Serial.println("FATAL audio task allocation");
        text(15, 140, "AUDIO TASK ERROR", CORAL);
        while (true) delay(1000);
    }
    if (firstRun) toast("PRESS PLAY TO HEAR THE DEMO", 5000);
    if (!board::micAvailable()) toast("MIC UNAVAILABLE - SYNTH READY", 5000);
    screenIdle.activity(millis());
    Serial.printf("ORBIT_READY version=3.2.3 display=240x284 sample_rate=16000 mic=%d storage=%d heap=%u\n",
        board::micAvailable(), storageOk, ESP.getFreeHeap());
}

void loop() {
    serialControl();
    if (screenIdle.sleeping()) {
        pollSleeping();
        return;
    }
    pollTouch();
    pollButtons();
    pollBattery();
    if (page == Confirm && confirmation == RecordTrack) {
        int32_t remaining = int32_t(recordChoiceUntil - millis());
        if (remaining <= 0 && !touchDown) beginTrackRecording(true);
        else {
            int seconds = std::max<int32_t>(1, (remaining + 999) / 1000);
            if (seconds != recordChoiceSeconds) {
                recordChoiceSeconds = seconds;
                dirty = true;
            }
        }
    }
    Status s = snapshot();
    if (s.scene != shown.scene) dirty = true;
    static bool wasCapturing = false;
    if (s.capturing != wasCapturing) {
        board::amplifier(!s.capturing);
        if (wasCapturing) {
            toast(captureDrops.load() ? "CAPTURE GLITCH - TRY AGAIN" :
                s.sampleCount ? "SAMPLE READY" : "CAPTURE TOO SHORT");
        }
        wasCapturing = s.capturing;
        dirty = true;
    }
    static uint32_t observedRevision = 0;
    if (s.revision != observedRevision) {
        lastEdit = millis();
        observedRevision = s.revision;
    }
    if (storageOk && s.revision != savedRevision && !s.playing && !s.capturing &&
        !touchDown && s.livePad < 0 && millis() - lastEdit > 1000) saveSession();
    uint32_t now = millis();
    bool audioIdle;
    { Lock lock; audioIdle = engine.idle(); }
    bool keepAwake = !audioIdle || s.playing || s.recording || s.armed || s.capturing ||
        s.fxRecording || s.fxArmed || s.fxTouched || touchDown || suppressTouch ||
        referenceHz.load() || speakerMeterPending.load() || inputProbePending.load() ||
        (page == Confirm && confirmation == RecordTrack) ||
        (storageOk && s.revision != savedRevision);
    if (keepAwake && screenIdle.sleeping()) userActivity();
    if (screenIdle.sleepIfIdle(now, keepAwake)) {
        if (!enterSleep()) {
            userActivity();
            toast("SLEEP FAILED - TRY RESTART", 4000);
        }
        return;
    }
    if (toastText[0] && int32_t(now - toastUntil) >= 0) {
        toastText[0] = 0;
        dirty = true;
    }
    if (!screenIdle.sleeping() && now - lastDraw >= 33) {
        lastDraw = now;
        bool preparationChanged = s.preparation != shown.preparation ||
            s.countdown != shown.countdown || s.preparationLoop != shown.preparationLoop;
        if (batteryDirty && !dirty && batteryHeaderVisible()) drawBattery();
        if (dirty || (page == Mic && s.capturing)) draw(s);
        else if (page == Play) {
            for (int i = 0; i < 8; ++i)
                if (((shown.activePads[s.selected] ^ s.activePads[s.selected]) & (1 << i)) ||
                    (s.livePad != shown.livePad && (i == s.livePad || i == shown.livePad)))
                    drawPad(i, s);
            if (s.step != shown.step || s.playing != shown.playing || s.armed != shown.armed ||
                s.recording != shown.recording || preparationChanged ||
                now < toastUntil || now - toastUntil < 60) {
                drawProgress(s);
                if (s.playing != shown.playing || s.armed != shown.armed || s.recording != shown.recording)
                    drawTransport(s);
            }
            shown = s;
        } else if (page == Mixer) {
            updateMixerPlayhead(shown, s);
            shown = s;
        } else if (page == Scenes) {
            if (shown.scene != s.scene || shown.nextScene != s.nextScene || shown.playing != s.playing)
                drawScenes(s);
            shown = s;
        } else if (page == Slices) {
            for (int pad = 0; pad < 8; ++pad)
                if (((shown.activePads[3] ^ s.activePads[3]) & (1 << pad)) ||
                    (shown.livePad != s.livePad && (pad == shown.livePad || pad == s.livePad)))
                    drawSlicePad(pad, s);
            if (shown.recording != s.recording || shown.armed != s.armed ||
                shown.playing != s.playing || preparationChanged) {
                drawTransport(s);
                drawSliceHint(s);
            }
            shown = s;
        } else if (page == LiveFx) {
            if (shown.effect.x != s.effect.x || shown.effect.y != s.effect.y ||
                shown.effect.active != s.effect.active) drawFxDot(shown, s);
            if (shown.fxRecording != s.fxRecording || shown.fxArmed != s.fxArmed ||
                shown.fxEnabled != s.fxEnabled || shown.playing != s.playing || preparationChanged)
                drawFxState(s);
            shown = s;
        }
        if (backlightPending) {
            digitalWrite(6, HIGH);
            backlightPending = false;
        }
    }
    if (now - lastHealth > 10000) {
        lastHealth = now;
        Serial.printf("HEALTH heap=%u min_heap=%u max_render_us=%u audio_errors=%u touch_errors=%u\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), audioMaxUs.load(), audioErrors.load(), touchErrors);
    }
    delay(1);
}
