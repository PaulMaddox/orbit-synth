#include "../Orbit/engine.h"
#include "../Orbit/audio_input.h"
#include "../Orbit/screen_idle.h"
#include "../Orbit/ui_layout.h"
#include "../Orbit/touch_gesture.h"
#include "sound_loudness.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace orbit;
namespace {
uint64_t energy = 0;
void advance(Engine &engine, uint32_t samples) {
    int16_t block[127];
    while (samples) {
        size_t count = std::min(samples, uint32_t(127));
        engine.render(block, count);
        for (size_t i = 0; i < count; ++i) {
            assert(std::abs(int(block[i])) <= 30000);
            energy += std::abs(int(block[i]));
        }
        samples -= count;
    }
}

void waitForRecording(Engine &engine) {
    uint32_t remaining = SampleRate * 41;
    while ((engine.status().armed || engine.status().fxArmed) && remaining >= 64) {
        advance(engine, 64);
        remaining -= 64;
    }
    assert(engine.status().recording || engine.status().fxRecording);
}

double speakerRms(Engine &engine, uint32_t samples, std::vector<int16_t> *audio = nullptr) {
    int16_t block[64];
    double previous = 0, highpass = 0, squared = 0;
    for (uint32_t offset = 0; offset < samples; offset += 64) {
        size_t count = std::min(uint32_t(64), samples - offset);
        engine.render(block, count);
        if (audio) audio->insert(audio->end(), block, block + count);
        for (size_t i = 0; i < count; ++i) {
            assert(std::abs(int(block[i])) <= 30000);
            double input = block[i];
            highpass = 0.895 * (highpass + input - previous);
            previous = input;
            squared += highpass * highpass;
        }
    }
    return std::sqrt(squared / samples);
}

double frequencyShare(const std::vector<int16_t> &audio, double frequency) {
    double real = 0, imaginary = 0, squared = 0;
    for (size_t i = 0; i < audio.size(); ++i) {
        double phase = i * 2 * 3.14159265358979323846 * frequency / SampleRate;
        double value = audio[i] * (0.5 - 0.5 * std::cos(i * 2 * 3.14159265358979323846 /
            (audio.size() - 1)));
        real += value * std::cos(phase);
        imaginary += value * std::sin(phase);
        squared += value * value;
    }
    return squared ? 2 * (real * real + imaginary * imaginary) / (audio.size() * squared) : 0;
}

void assertCornerSafe(const ui::Rect &rect) {
    // Conservative design envelope for the rounded viewing window, not a measured panel radius.
    constexpr int radius = 48;
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            assert(x >= 0 && x < 240 && y >= 0 && y < 284);
            int dx = x < radius ? radius - x : std::max(0, x - (240 - radius));
            int dy = y < radius ? radius - y : std::max(0, y - (284 - radius));
            assert(dx * dx + dy * dy <= radius * radius);
        }
    }
}

template <size_t N>
void assertFooterCoverage(const ui::Rect (&buttons)[N], const ui::Rect (&targets)[N]) {
    for (size_t i = 0; i < N; ++i) {
        const auto &button = buttons[i];
        for (int y = button.y; y < button.y + button.h; ++y)
            for (int x = button.x; x < button.x + button.w; ++x)
                assert(ui::hit(targets, x, y) == int(i));
    }
    for (int x = 0; x < 240; ++x) {
        assert(ui::hit(targets, x, 231) == -1);
        assert(ui::hit(targets, x, 284) == -1);
        for (int y = 232; y < 284; ++y) {
            int matches = 0;
            for (const auto &target : targets) matches += target.contains(x, y);
            assert(matches == 1);
        }
    }
    assert(ui::hit(targets, -1, 252) == -1);
    assert(ui::hit(targets, 240, 252) == -1);
}
}

int main() {
    assertCornerSafe(ui::HeaderStatus);
    assertCornerSafe(ui::Back);
    assertCornerSafe(ui::BackTouch);
    for (const auto &rect : ui::Footer) assertCornerSafe(rect);
    for (const auto &rect : ui::FooterPair) assertCornerSafe(rect);
    assertCornerSafe(ui::FooterWide);
    assertCornerSafe(ui::SoundPrevious);
    assertCornerSafe(ui::SoundNext);
    for (const auto &rect : ui::SoundCards) assertCornerSafe(rect);
    assert(ui::hit(ui::SoundCards, 9, 113) == 0);
    assert(ui::hit(ui::SoundCards, 230, 205) == 3);
    assert(ui::hit(ui::SoundCards, 120, 170) == -1);
    assert(ui::hit(ui::Footer, 25, 264) == 0);
    assert(ui::hit(ui::Footer, 218, 264) == 2);
    assert(ui::hit(ui::Footer, 10, 271) == -1);
    assert(ui::hit(ui::FooterPair, 25, 264) == 0);
    assert(ui::hit(ui::FooterPair, 218, 264) == 1);
    assertFooterCoverage(ui::Footer, ui::FooterTouch);
    assertFooterCoverage(ui::FooterPair, ui::FooterPairTouch);
    const ui::Rect wideButton[] = {ui::FooterWide}, wideTarget[] = {ui::FooterWideTouch};
    assertFooterCoverage(wideButton, wideTarget);
    for (const auto &button : ui::Footer) {
        for (int x : {button.x, button.x + button.w - 1}) {
            for (int y : {button.y, button.y + button.h - 1}) {
                for (uint32_t duration : {100u, 649u, 650u, 651u, 1000u, 2500u}) {
                    ui::TouchGesture press;
                    press.begin(x, y, 100);
                    press.move(x + 2, y - 2);
                    assert(!press.takeHold(100 + duration, false));
                    assert(press.tapped());
                    assert(ui::hit(ui::FooterTouch, press.x(), press.y()) ==
                        ui::hit(ui::Footer, x, y));
                }
            }
        }
    }
    ui::TouchGesture hold;
    hold.begin(30, 50, 100);
    assert(!hold.takeHold(750, true));
    assert(hold.takeHold(751, true));
    assert(!hold.takeHold(1200, true));
    assert(!hold.tapped());
    hold.begin(30, 252, 100);
    hold.move(53, 275);
    assert(!hold.takeHold(1100, false));
    assert(hold.tapped());
    hold.move(54, 275);
    assert(!hold.tapped());
    hold.begin(30, 50, 100);
    hold.move(44, 50);
    assert(!hold.takeHold(1100, true));
    assert(hold.tapped());
    hold.begin(30, 252, UINT32_MAX - 400);
    assert(!hold.takeHold(400, false));
    assert(hold.tapped());
    assert(hold.takeHold(400, true));
    assert(!hold.tapped());
    assert(ui::HeaderStatus.contains(ui::HeaderRight, 17));
    assertCornerSafe(ui::Navigation);
    for (const auto &rect : ui::MenuCards) assertCornerSafe(rect);
    for (const auto &rect : ui::SceneCards) assertCornerSafe(rect);
    assertCornerSafe(ui::SceneCopy);
    assertCornerSafe(ui::FxPad);
    for (const auto &rect : ui::FxOptions) assertCornerSafe(rect);
    for (int pad = 0; pad < 8; ++pad) {
        auto rect = ui::slicePad(pad);
        assertCornerSafe(rect);
        assert(ui::sliceAt(rect.x + 5, rect.y + 5) == pad);
    }

    ScreenIdle screen;
    screen.activity(1000);
    assert(!screen.sleepIfIdle(60999));
    assert(screen.sleepIfIdle(61000) && screen.sleeping());
    assert(!screen.sleepIfIdle(62000));
    assert(screen.activity(62000) && !screen.sleeping());
    assert(!screen.sleepIfIdle(121999));
    assert(!screen.activity(122000));
    assert(!screen.sleepIfIdle(181999));
    screen.activity(UINT32_MAX - 1000);
    assert(!screen.sleepIfIdle(58998));
    assert(screen.sleepIfIdle(58999));
    assert(screen.activity(59000));
    assert(!screen.sleepIfIdle(180000, true) && !screen.sleeping());
    assert(!screen.sleepIfIdle(239999));
    assert(screen.sleepIfIdle(240000));
    assert(screen.activity(240001));

    auto engine = std::make_unique<Engine>();
    assert(Engine::valid(engine->session));
    assert(engine->select(-1) == Result::Invalid);
    assert(engine->noteOn(8) == Result::Invalid);
    assert(engine->select(3) == Result::Ok);
    assert(engine->noteOn(0) == Result::NoSample);
    assert(engine->toggleRecord() == Result::NoSample);
    assert(Engine::midiNote(0, 1, 0, 0) == 63);
    assert(Engine::midiNote(1, 0, 0, 0) == 48);

    engine->loadDemo();
    const uint16_t demoLeadCount = engine->session.tracks[0].count;
    engine->setBpm(110);
    engine->setSwing(0);
    assert(Engine::valid(engine->session));
    engine->togglePlay();
    energy = 0;
    advance(*engine, 48000);
    assert(energy > 1000000);
    uint32_t expected = uint64_t(48000) * 110 * 96 / (SampleRate * 60) % LoopTicks;
    assert(engine->status().tick == expected);
    assert(engine->select(1) == Result::Ok);
    assert(engine->toggleRecord() == Result::Ok);
    assert(engine->status().armed);
    assert(engine->select(0) == Result::Busy);
    advance(*engine, 22000);
    waitForRecording(*engine);
    assert(engine->status().recording);
    engine->stop();

    engine->select(0);
    engine->clearSelected();
    assert(engine->session.tracks[0].count == 0);
    engine->undo();
    assert(engine->session.tracks[0].count == demoLeadCount);
    engine->undo();
    assert(engine->session.tracks[0].count == 0);

    assert(engine->toggleRecord() == Result::Ok);
    assert(engine->status().armed);
    assert(engine->toggleRecord() == Result::Ok);
    assert(!engine->status().playing);
    assert(!engine->status().armed);

    assert(engine->toggleRecord() == Result::Ok);
    waitForRecording(*engine);
    assert(engine->status().recording);
    assert(engine->noteOn(2) == Result::Ok);
    advance(*engine, 4096);
    engine->noteOff();
    assert(engine->session.tracks[0].count == 1);
    assert(engine->session.tracks[0].notes[0].tick == 0);
    assert(engine->session.tracks[0].notes[0].duration == 48);
    engine->toggleRecord();
    assert(!engine->status().recording);
    engine->undo();
    assert(engine->session.tracks[0].count == 0);
    engine->undo();
    assert(engine->session.tracks[0].count == 1);
    engine->stop();

    for (int patch = 0; patch < 4; ++patch) {
        engine->setPatch(patch);
        energy = 0;
        engine->noteOn(7);
        advance(*engine, 4000);
        engine->noteOff();
        advance(*engine, 4000);
        assert(energy > 50000);
    }
    engine->setBpm(999); assert(engine->session.bpm == 180);
    engine->setBpm(0); assert(engine->session.bpm == 60);
    engine->setVolume(999); assert(engine->session.volume == 80);
    engine->setVolume(-1); assert(engine->session.volume == 0);
    engine->setVolume(35);

    engine->startCapture();
    assert(!engine->status().playing && engine->status().capturing);
    assert(engine->noteOn(0) == Result::Busy);
    int16_t frames[4 * 64];
    for (int i = 0; i < 64; ++i) {
        frames[i * 4] = 1000;
        frames[i * 4 + 1] = int16_t(2000 + 3000 * std::sin(i * 0.3));
        frames[i * 4 + 2] = 20000;
        frames[i * 4 + 3] = 0;
    }
    for (int i = 0; i < 550; ++i) engine->capture(frames, 64, 4, 1);
    assert(!engine->status().capturing);
    assert(engine->session.sampleCount == SampleCapacity);
    assert(engine->sample[0] == 0 && engine->sample[SampleCapacity - 1] == 0);
    assert(Engine::valid(engine->session));
    engine->select(3);
    energy = 0;
    engine->noteOn(0);
    advance(*engine, 16000);
    assert(energy > 1000000);
    engine->noteOff();
    assert(engine->audition() == Result::Ok);
    assert(!engine->idle());
    advance(*engine, 60000);
    assert(engine->status().livePad == -1);
    assert(engine->idle());

    auto invalid = engine->session;
    invalid.tracks[1].count = MaxNotes + 1;
    assert(!Engine::valid(invalid));
    invalid = engine->session;
    invalid.tracks[0].notes[0].duration = 0;
    assert(!Engine::valid(invalid));
    invalid = engine->session;
    invalid.sampleCount = SampleCapacity + 1;
    assert(!Engine::valid(invalid));
    invalid = engine->session;
    invalid.tracks[0].pitch = 13;
    assert(!Engine::valid(invalid));
    invalid.tracks[0].pitch = -13;
    assert(!Engine::valid(invalid));
    invalid = engine->session;
    invalid.tracks[0].patch = SoundCount;
    assert(!Engine::valid(invalid));

    std::vector<bool> reachable(SoundCount, false);
    for (int family = 0; family < SoundFamilyCount; ++family) {
        int count = 0;
        for (int position = 0; position < 4; ++position) {
            int patch = familySound(family, position);
            if (patch < 0) continue;
            assert(patch < SoundCount && !reachable[patch]);
            reachable[patch] = true;
            ++count;
        }
        assert(count >= 3 && familySound(family, 4) == -1);
    }
    assert(std::all_of(reachable.begin(), reachable.end(), [](bool value) { return value; }));

    assert(InstrumentBankBytes < 2200000);
    for (size_t i = 0; i < InstrumentSampleCount; ++i) {
        const auto &asset = InstrumentSamples[i];
        assert(asset.data >= InstrumentBank && asset.data + asset.frames <= InstrumentBank + InstrumentBankBytes);
        assert(asset.frames > 4096 && asset.root >= 21 && asset.root <= 108);
        assert(!asset.loopEnd || (asset.loopStart < asset.loopEnd && asset.loopEnd < asset.frames));
    }
    double tapMinimum[2][3], tapMaximum[2][3]{};
    int tapQuiet[2][3]{}, tapLoud[2][3]{};
    for (auto &role : tapMinimum) std::fill_n(role, 3, 1e9);
    for (int patch = 0; patch < SoundCount; ++patch) {
        bool recorded = Sounds[patch].wave == SoundWave::Sample;
        const auto *asset = findInstrumentSample(patch, 60, 108);
        assert(bool(asset) == recorded);
        if (recorded) {
            for (int midi = 36; midi <= 100; ++midi) {
                int natural = instrumentMidi(patch, midi);
                assert(natural >= 28 && natural <= 100 && (natural - midi) % 12 == 0);
            }
        }
        auto articulation = std::make_unique<Engine>();
        articulation->setPatch(patch);
        articulation->setSpace(0);
        articulation->setBpm(120);
        articulation->noteOn(0);
        advance(*articulation, 32);
        articulation->noteOff();
        advance(*articulation, SampleRate / 10);
        assert(articulation->status().activePads[0] && articulation->status().livePad == -1);
        advance(*articulation, SampleRate);
        assert(articulation->idle());
        articulation->noteOn(0);
        advance(*articulation, SampleRate * 3);
        if (!recorded || asset->loopEnd) assert(articulation->status().activePads[0]);
        articulation->noteOff();
        advance(*articulation, SampleRate / 125);
        assert(!articulation->status().activePads[0]);
        advance(*articulation, SampleRate / 4);
        assert(articulation->idle());
        for (int track : {0, 1}) for (int bpm : {60, 120, 180}) {
            auto tap = std::make_unique<Engine>();
            tap->select(track);
            tap->setPatch(patch);
            tap->setSpace(0);
            tap->setBpm(bpm);
            uint32_t length = (SampleRate * 30 + bpm - 1) / bpm;
            tap->noteOn(0);
            std::vector<int16_t> pcm(length);
            tap->render(pcm.data(), 32);
            tap->noteOff();
            tap->render(pcm.data() + 32, length - 33);
            assert(tap->status().activePads[track]);
            tap->render(pcm.data() + length - 1, 1);
            assert(!tap->status().activePads[track]);
            auto weighted = test::speakerSignal(pcm.data(), pcm.size());
            double rms = test::noteLoudness(weighted.data(), weighted.size()) *
                test::speakerAdjustment(track, patch, Engine::midiNote(track, 0, 0, 0));
            int tempo = bpm == 60 ? 0 : bpm == 120 ? 1 : 2;
            if (rms < tapMinimum[track][tempo]) tapQuiet[track][tempo] = patch;
            if (rms > tapMaximum[track][tempo]) tapLoud[track][tempo] = patch;
            tapMinimum[track][tempo] = std::min(tapMinimum[track][tempo], rms);
            tapMaximum[track][tempo] = std::max(tapMaximum[track][tempo], rms);
            assert(tap->previewSound(patch) == Result::Ok);
            advance(*tap, length - 1);
            assert(tap->status().activePads[track]);
            advance(*tap, 1);
            assert(!tap->status().activePads[track]);
        }
    }
    for (int track = 0; track < 2; ++track) for (int tempo = 0; tempo < 3; ++tempo) {
        std::printf("TAP_LOUDNESS track=%d bpm=%d %.1f(%d)..%.1f(%d) weighted RMS (%.2f dB)\n",
            track, tempo == 0 ? 60 : tempo == 1 ? 120 : 180,
            tapMinimum[track][tempo], tapQuiet[track][tempo],
            tapMaximum[track][tempo], tapLoud[track][tempo],
            20 * std::log10(tapMaximum[track][tempo] / tapMinimum[track][tempo]));
        std::fflush(stdout);
        assert(20 * std::log10(tapMaximum[track][tempo] / tapMinimum[track][tempo]) <= 3);
    }
    auto preparation = std::make_unique<Engine>();
    preparation->loadDemo();
    preparation->setBpm(120);
    auto beforePreparation = std::make_unique<Session>(preparation->session);
    uint32_t preparationRevision = preparation->revision();
    preparation->toggleRecord(true);
    for (int beat = 0; beat < 32; ++beat) {
        Status status = preparation->status();
        assert(status.armed && !status.recording);
        assert(status.preparation == (beat < 16 ?
            RecordPreparation::GetReady : RecordPreparation::Practice));
        assert(status.countdown == 8 - beat % 8);
        assert(status.preparationLoop == 1 + (beat % 16) / 8);
        assert(preparation->revision() == preparationRevision);
        assert(std::memcmp(preparation->session.tracks, beforePreparation->tracks,
            sizeof(beforePreparation->tracks)) == 0);
        if (beat < 16) assert(status.activePads[1] == 0);
        if (beat == 16) assert(status.activePads[1] != 0 && status.activePads[0] == 0);
        preparation->noteOn(beat % 8);
        advance(*preparation, 32);
        preparation->noteOff();
        assert(preparation->session.tracks[0].count == beforePreparation->tracks[0].count);
        advance(*preparation, SampleRate / 2 - 33);
        assert(preparation->status().armed && !preparation->status().recording);
        advance(*preparation, 1);
    }
    assert(preparation->status().recording && !preparation->status().armed);
    assert(preparation->status().preparation == RecordPreparation::None);
    assert(preparation->session.tracks[0].count == 0);
    assert(std::memcmp(preparation->session.tracks + 1, beforePreparation->tracks + 1,
        sizeof(Track) * (TrackCount - 1)) == 0);
    preparation->noteOn(2);
    advance(*preparation, 32);
    preparation->noteOff();
    assert(preparation->session.tracks[0].count == 1);
    preparation->stop();
    preparation->undo();
    assert(std::memcmp(preparation->session.tracks, beforePreparation->tracks,
        sizeof(beforePreparation->tracks)) == 0);

    auto cancelPreparation = std::make_unique<Engine>();
    cancelPreparation->loadDemo();
    cancelPreparation->setBpm(120);
    auto beforeCancel = std::make_unique<Session>(cancelPreparation->session);
    cancelPreparation->toggleRecord(true);
    advance(*cancelPreparation, SampleRate * 8);
    assert(cancelPreparation->status().preparation == RecordPreparation::Practice);
    cancelPreparation->toggleRecord();
    assert(!cancelPreparation->status().playing && !cancelPreparation->status().armed);
    assert(cancelPreparation->status().preparation == RecordPreparation::None);
    assert(std::memcmp(cancelPreparation->session.tracks, beforeCancel->tracks,
        sizeof(beforeCancel->tracks)) == 0);
    cancelPreparation->togglePlay();
    advance(*cancelPreparation, SampleRate / 4);
    cancelPreparation->toggleRecord(true);
    assert(cancelPreparation->status().preparation == RecordPreparation::Waiting);
    advance(*cancelPreparation, SampleRate * 4 - SampleRate / 4 - 1);
    assert(cancelPreparation->status().preparation == RecordPreparation::Waiting);
    advance(*cancelPreparation, 1);
    assert(cancelPreparation->status().preparation == RecordPreparation::GetReady);
    advance(*cancelPreparation, SampleRate * 8);
    assert(cancelPreparation->status().preparation == RecordPreparation::Practice);
    cancelPreparation->toggleRecord();
    assert(cancelPreparation->status().playing && !cancelPreparation->status().armed);
    assert(cancelPreparation->status().preparation == RecordPreparation::None);
    advance(*cancelPreparation, SampleRate * 8);
    assert(!cancelPreparation->status().recording);
    cancelPreparation->stop();
    cancelPreparation->toggleRecord(true);
    cancelPreparation->undo();
    assert(!cancelPreparation->status().playing);
    assert(cancelPreparation->status().preparation == RecordPreparation::None);
    cancelPreparation->toggleRecord(true);
    cancelPreparation->clearSelected();
    assert(!cancelPreparation->status().playing);
    assert(cancelPreparation->status().preparation == RecordPreparation::None);

    auto preparationTempo = std::make_unique<Engine>();
    preparationTempo->setBpm(60);
    preparationTempo->toggleRecord();
    advance(*preparationTempo, SampleRate * 8 - 1);
    assert(preparationTempo->status().preparationLoop == 1);
    assert(preparationTempo->status().countdown == 1);
    advance(*preparationTempo, 1);
    assert(preparationTempo->status().preparationLoop == 2);
    assert(preparationTempo->status().countdown == 8);
    preparationTempo->setBpm(180);
    advance(*preparationTempo, (SampleRate * 60 * 8 + 179) / 180);
    assert(preparationTempo->status().preparation == RecordPreparation::Practice);
    assert(preparationTempo->status().preparationLoop == 1);
    assert(preparationTempo->status().countdown == 8);
    preparationTempo->stop();
    assert(!preparationTempo->status().armed);

    auto changingTempo = std::make_unique<Engine>();
    changingTempo->setBpm(60);
    changingTempo->setPatch(18);
    changingTempo->setSpace(0);
    changingTempo->noteOn(0);
    advance(*changingTempo, 32);
    changingTempo->noteOff();
    advance(*changingTempo, 1568);
    changingTempo->setBpm(180);
    advance(*changingTempo, 2133);
    assert(changingTempo->status().activePads[0]);
    advance(*changingTempo, 1);
    assert(!changingTempo->status().activePads[0]);

    auto gestures = std::make_unique<Engine>();
    gestures->setBpm(120);
    gestures->setSpace(0);
    gestures->toggleRecord();
    waitForRecording(*gestures);
    assert(gestures->status().recording);
    gestures->noteOn(0);
    advance(*gestures, 32);
    gestures->noteOff();
    gestures->noteOn(3);
    advance(*gestures, SampleRate / 2);
    gestures->noteOff();
    gestures->stop();
    assert(gestures->session.tracks[0].count == 2);
    assert(gestures->session.tracks[0].notes[0].duration == 48);
    assert(gestures->session.tracks[0].notes[1].duration == 96);
    gestures->copyScene(1);
    gestures->clearSelected();
    gestures->undo();
    assert(gestures->session.tracks[0].notes[0].duration == 48);
    gestures->queueScene(1);
    assert(gestures->session.tracks[0].notes[1].duration == 96);

    auto acrossCountIn = std::make_unique<Engine>();
    acrossCountIn->setBpm(120);
    acrossCountIn->toggleRecord();
    acrossCountIn->noteOn(0);
    advance(*acrossCountIn, SampleRate * 16 + SampleRate / 10);
    acrossCountIn->noteOff();
    assert(acrossCountIn->session.tracks[0].notes[0].duration == 24);

    auto replay = std::make_unique<Engine>();
    replay->setBpm(120);
    replay->setPatch(18);
    replay->setSpace(0);
    replay->session.tracks[0].count = 1;
    replay->session.tracks[0].notes[0] = Note{0, 48, 0, 108};
    replay->togglePlay();
    std::vector<int16_t> tapAudio(4000);
    replay->render(tapAudio.data(), tapAudio.size());
    assert(!replay->status().activePads[0]);
    std::vector<double> tapSignal(tapAudio.begin(), tapAudio.end());
    assert(test::rms(tapSignal.data() + 3984, 16) <
           test::rms(tapSignal.data() + 3200, 256) * 0.1);
    replay->stop();
    replay->setBpm(60);
    replay->togglePlay();
    advance(*replay, 1600);
    replay->setBpm(180);
    advance(*replay, 2133);
    assert(replay->status().activePads[0]);
    advance(*replay, 1);
    assert(!replay->status().activePads[0]);

    for (uint16_t version : {1, 2, 3, 4}) {
        auto migration = std::make_unique<Engine>();
        migration->session.version = version;
        for (auto &track : migration->session.tracks)
            track.reserved = version < 4 ? 255 : 1;
        for (auto &scene : migration->session.scenes)
            for (auto &track : scene.tracks) track.reserved = 255;
        if (version == 4) Engine::initializeScenes(migration->session);
        if (version == 1) migration->session.tracks[0].pitch = 127;
        Engine::upgradeSession(migration->session, version);
        assert(Engine::valid(migration->session));
        assert(migration->session.tracks[0].reserved == 0);
        if (version == 1) assert(migration->session.tracks[0].pitch == 0);
    }
    for (int legacyMode : {0, 1, 2}) for (int patch : {0, 18}) {
        auto legacy = std::make_unique<Session>();
        legacy->version = 4;
        legacy->tracks[0].patch = patch;
        legacy->tracks[0].reserved = legacyMode;
        legacy->tracks[0].count = 1;
        legacy->tracks[0].notes[0] = Note{0, 24, 0, 108};
        Engine::initializeScenes(*legacy);
        Engine::upgradeSession(*legacy, 4);
        assert(Engine::valid(*legacy));
        int duration = legacyMode == 1 || (legacyMode == 0 && patch == 0) ? 48 : 24;
        assert(legacy->tracks[0].notes[0].duration == duration);
        assert(legacy->scenes[3].tracks[0].notes[0].duration == duration);
    }

    double loudnessMinimum = 1e9, loudnessMaximum = 0, worstSpreadDb = 0;
    for (int track = 0; track < 2; ++track) {
        for (int pitch : {-12, 0, 12}) {
            for (int pad : {0, 3, 7}) {
                for (int root : {0, 5, 11}) {
                    for (int tone : {0, 35, 60, 82, 94, 100}) {
                        double noteMin = 1e9, noteMax = 0;
                        int quiet = 0, loud = 0;
                        for (int patch = 0; patch < SoundCount; ++patch) {
                            auto sound = std::make_unique<Engine>();
                            sound->select(track);
                            sound->setPatch(patch);
                            sound->setSpace(0);
                            sound->setRoot(root);
                            sound->setTone(tone);
                            sound->setPitch(pitch);
                            sound->noteOn(pad);
                            int16_t pcm[6400];
                            sound->render(pcm, 6400);
                            for (int i = 0; i < 6400; ++i) {
                                assert(std::abs(int(pcm[i])) < 29999);
                            }
                            auto weighted = test::speakerSignal(pcm, 6400);
                            int midi = Engine::midiNote(track, pad, root, 0) + pitch;
                            double adjustment = test::speakerAdjustment(track, patch, midi);
                            double rms = test::noteLoudness(weighted.data(), 6400) * adjustment;
                            if (rms < noteMin) quiet = patch;
                            if (rms > noteMax) loud = patch;
                            noteMin = std::min(noteMin, rms);
                            noteMax = std::max(noteMax, rms);
                            loudnessMinimum = std::min(loudnessMinimum, rms);
                            loudnessMaximum = std::max(loudnessMaximum, rms);
                        }
                        double spread = 20 * std::log10(noteMax / noteMin);
                        if (spread > std::max(3.0, worstSpreadDb))
                            std::printf("LOUDNESS_SPREAD track=%d pitch=%d pad=%d root=%d tone=%d %.2fdB(%d,%d)\n",
                                track, pitch, pad, root, tone, spread, quiet, loud);
                        worstSpreadDb = std::max(worstSpreadDb, spread);
                        if (!(noteMin > 60 && noteMax < 84)) {
                            std::printf("LOUDNESS_FAILURE track=%d pitch=%d pad=%d root=%d tone=%d %.1f(%d)..%.1f(%d)\n",
                                track, pitch, pad, root, tone, noteMin, quiet, noteMax, loud);
                            std::fflush(stdout);
                        }
                        assert(noteMin > 60 && noteMax < 84);
                    }
                }
            }
        }
    }
    std::printf("LOUDNESS all sounds, registers and tones: %.1f..%.1f weighted RMS\n",
        loudnessMinimum, loudnessMaximum);
    std::fflush(stdout);
    assert(worstSpreadDb <= 3);

    for (int track = 0; track < 2; ++track) {
        double profiles[3][8]{};
        const int patches[] = {1, 16, 27};
        for (int sound = 0; sound < 3; ++sound) {
            auto pad = std::make_unique<Engine>();
            pad->select(track);
            pad->setPatch(patches[sound]);
            pad->setSpace(0);
            pad->noteOn(0);
            advance(*pad, 1024);
            std::vector<int16_t> audio(8192);
            pad->render(audio.data(), audio.size());
            double f = 440 * std::pow(2.0, (Engine::midiNote(track, 0, 0, 0) - 69) / 12.0);
            double sum = 0;
            for (int harmonic = 0; harmonic < 8; ++harmonic) {
                profiles[sound][harmonic] = frequencyShare(audio, f * (harmonic + 1));
                sum += profiles[sound][harmonic];
            }
            for (double &share : profiles[sound]) share /= sum;
        }
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < a; ++b) {
                double distance = 0;
                for (int h = 0; h < 8; ++h) distance += std::abs(profiles[a][h] - profiles[b][h]);
                std::printf("PAD_TIMBRE track=%d sounds=%d,%d spectral_distance=%.3f\n",
                    track, patches[a], patches[b], distance);
                std::fflush(stdout);
                assert(distance > 0.25);
            }
        }
    }

    for (int track = 0; track < 2; ++track) {
        for (int patch = 0; patch < SoundCount; ++patch) {
            for (int pitch : {-12, 0, 12}) {
                for (int pad : {0, 7}) {
                    auto sound = std::make_unique<Engine>();
                    sound->select(track);
                    sound->setPatch(patch);
                    sound->setSpace(0);
                    sound->setTone(100);
                    sound->setPitch(pitch);
                    sound->setVolume(80);
                    sound->noteOn(pad);
                    int16_t pcm[6400];
                    sound->render(pcm, 6400);
                    int clipped = 0;
                    for (int16_t value : pcm) if (std::abs(int(value)) >= 29999) ++clipped;
                    if (clipped) {
                        std::printf("HEADROOM track=%d patch=%d pitch=%d pad=%d clipped=%d\n",
                            track, patch, pitch, pad, clipped);
                        std::fflush(stdout);
                    }
                    assert(!clipped);
                }
            }
        }
    }

    for (int track = 0; track < 2; ++track) {
        std::vector<std::vector<int16_t>> timbres;
        for (int patch = 0; patch < SoundCount; ++patch) {
            auto sound = std::make_unique<Engine>();
            sound->select(track);
            sound->setPatch(patch);
            sound->setSpace(0);
            assert(Engine::valid(sound->session));
            sound->noteOn(0);
            std::vector<int16_t> audio;
            double rms = speakerRms(*sound, SampleRate / 2, &audio);
            std::printf("SOUND track=%d patch=%d %s speaker_band_rms=%.1f\n",
                track, patch, Sounds[patch].name, rms);
            sound->noteOff();
            advance(*sound, SampleRate * 5);
            assert(sound->idle());
            timbres.push_back(audio);

            uint32_t revision = sound->revision();
            assert(sound->previewSound((patch + 1) % SoundCount) == Result::Ok);
            assert(sound->session.tracks[track].patch == patch);
            advance(*sound, SampleRate * 2);
            assert(sound->idle() && sound->status().livePad == -1);
            assert(sound->revision() == revision);
        }
        double closest = 0;
        for (int a = 0; a < SoundCount; ++a) {
            for (int b = 0; b < a; ++b) {
                double dot = 0, aa = 0, bb = 0;
                for (size_t i = 0; i < timbres[a].size(); ++i) {
                    double x = timbres[a][i], y = timbres[b][i];
                    dot += x * y; aa += x * x; bb += y * y;
                }
                double correlation = std::abs(dot) / std::sqrt(aa * bb);
                closest = std::max(closest, correlation);
                if (correlation >= 0.999)
                    std::printf("SIMILAR_SOUNDS track=%d a=%d b=%d correlation=%.6f\n",
                        track, a, b, correlation);
                assert(correlation < 0.999);
            }
        }
        std::printf("SOUND_DISTINCTNESS track=%d closest_correlation=%.6f\n", track, closest);
    }

    auto preview = std::make_unique<Engine>();
    preview->loadDemo();
    preview->toggleRecord(false);
    waitForRecording(*preview);
    assert(preview->status().recording);
    std::vector<uint8_t> saved(sizeof(Session));
    std::memcpy(saved.data(), &preview->session, saved.size());
    std::vector<int16_t> savedSample(preview->sample, preview->sample + preview->session.sampleCount);
    uint32_t revision = preview->revision();
    for (int patch = 0; patch < SoundCount; ++patch) {
        assert(preview->previewSound(patch) == Result::Ok);
        advance(*preview, 256);
    }
    preview->stopPreview();
    assert(preview->status().recording && preview->status().livePad == -1);
    assert(preview->revision() == revision);
    assert(std::memcmp(saved.data(), &preview->session, saved.size()) == 0);
    assert(std::equal(savedSample.begin(), savedSample.end(), preview->sample));
    assert(preview->previewSound(-1) == Result::Invalid);
    assert(preview->previewSound(SoundCount) == Result::Invalid);
    assert(preview->revision() == revision);
    uint16_t beforeLiveNote = preview->session.tracks[0].count;
    preview->noteOn(2);
    assert(preview->previewSound(29) == Result::Ok);
    advance(*preview, 64);
    assert(preview->status().livePad == 2);
    assert(preview->session.tracks[0].count == beforeLiveNote);
    preview->stop();
    preview->select(2);
    assert(preview->previewSound(0) == Result::Invalid);
    preview->select(3);
    assert(preview->previewSound(0) == Result::Invalid);
    preview->select(0);
    preview->startCapture();
    revision = preview->revision();
    assert(preview->previewSound(0) == Result::Busy);
    assert(preview->status().capturing && preview->revision() == revision);

    auto crowded = std::make_unique<Engine>();
    crowded->setPatch(1);
    crowded->session.tracks[0].count = 12;
    for (int i = 0; i < 12; ++i)
        crowded->session.tracks[0].notes[i] = Note{0, LoopTicks, uint8_t(i % 8), 90};
    crowded->togglePlay();
    auto uninterrupted = std::make_unique<Engine>(*crowded);
    assert(crowded->previewSound(29) == Result::Busy);
    std::vector<int16_t> withAttempt(4096), withoutAttempt(4096);
    crowded->render(withAttempt.data(), withAttempt.size());
    uninterrupted->render(withoutAttempt.data(), withoutAttempt.size());
    assert(withAttempt == withoutAttempt);

    for (int track = 0; track < TrackCount; ++track) {
        for (int pitch : {-12, 12}) {
            auto pitched = std::make_unique<Engine>();
            pitched->select(track);
            pitched->setPatch(track < 2 ? 18 : 0);
            pitched->setSpace(0);
            pitched->setTone(100);
            double baseFrequency = track == 2 ? 1600 : track == 3 ? 1000 :
                440 * std::pow(2.0, (Engine::midiNote(track, 0, 0, 0) - 69) / 12.0);
            if (track == 3) {
                pitched->session.sampleCount = SampleRate;
                for (int i = 0; i < SampleRate; ++i)
                    pitched->sample[i] = int16_t(20000 * std::sin(i * 2 *
                        3.14159265358979323846 * baseFrequency / SampleRate));
            }
            pitched->noteOn(track == 2 ? 7 : 0);
            advance(*pitched, 128);
            // Retune an already sounding voice, not just subsequently triggered notes.
            pitched->setPitch(pitch * 10);
            std::vector<int16_t> audio(track == 2 ? 1024 : 4096);
            pitched->render(audio.data(), audio.size());
            double target = baseFrequency * std::pow(2.0, pitch / 12.0);
            double share = frequencyShare(audio, target);
            std::printf("PITCH track=%d semitones=%d frequency=%.1f share=%.3f\n",
                track, pitch, target, share);
            assert(share > 0.02);
            if (pitch > 0) assert(frequencyShare(audio, baseFrequency) < 0.005);
            assert(Engine::valid(pitched->session));

            pitched->stop();
            advance(*pitched, SampleRate);
            pitched->noteOn(track == 2 ? 7 : 0);
            advance(*pitched, 128);
            pitched->render(audio.data(), audio.size());
            assert(frequencyShare(audio, target) > 0.02);
        }
    }

    auto samplePitch = std::make_unique<Engine>();
    samplePitch->select(3);
    samplePitch->session.sampleCount = SampleRate;
    for (int i = 0; i < SampleRate; ++i) samplePitch->sample[i] = i % 16 < 8 ? 10000 : -10000;
    samplePitch->setPitch(-12);
    samplePitch->noteOn(0);
    samplePitch->noteOff();
    advance(*samplePitch, SampleRate);
    assert(samplePitch->status().activePads[3]);
    samplePitch->setPitch(12);
    advance(*samplePitch, SampleRate / 2);
    assert(!samplePitch->status().activePads[3]);

    for (int pad : {0, 5}) {
        auto drums = std::make_unique<Engine>();
        drums->select(2);
        drums->setSpace(0);
        drums->noteOn(pad);
        advance(*drums, 160);
        drums->noteOff();
        double rms = speakerRms(*drums, 1600);
        std::printf("DRUM pad=%d speaker_band_rms=%.1f\n", pad, rms);
        assert(rms > 200);
        assert(drums->status().activePads[2] & (1 << pad));
        drums->mute(2);
        advance(*drums, 8000);
        assert(drums->idle());
        drums->session.tracks[2].muted = false;
        drums->session.tracks[2].count = 1;
        drums->session.tracks[2].notes[0] = Note{0, 6, uint8_t(pad), 108};
        drums->togglePlay();
        advance(*drums, 1000);
        assert(drums->status().activePads[2] & (1 << pad));
        drums->clearSelected();
        advance(*drums, 8000);
        assert(drums->status().activePads[2] == 0);
        drums->stop();
    }

    auto oneShot = std::make_unique<Engine>();
    oneShot->session.sampleCount = SampleCapacity;
    oneShot->session.tracks[3].space = 0;
    for (int i = 8000; i < SampleCapacity; ++i)
        oneShot->sample[i] = int16_t(18000 * std::sin(i * 2 * 3.14159265358979323846 * 1000 / SampleRate));
    oneShot->select(3);
    oneShot->noteOn(0);
    advance(*oneShot, 160);
    oneShot->noteOff();
    advance(*oneShot, 7840);
    energy = 0;
    advance(*oneShot, 8000);
    assert(energy > 8000000);
    oneShot->stop();
    advance(*oneShot, 8000);
    assert(oneShot->idle());
    oneShot->session.tracks[3].count = 1;
    oneShot->session.tracks[3].notes[0] = Note{0, 6, 0, 108};
    oneShot->togglePlay();
    advance(*oneShot, 8000);
    energy = 0;
    advance(*oneShot, 8000);
    assert(energy > 8000000);
    oneShot->stop();

    auto input = std::make_unique<Engine>();
    input->startCapture();
    int16_t packed[64 * InputFrameSamples];
    for (int i = 0; i < 64; ++i) {
        packed[i * 4] = i % 2 ? 32760 : -32760;
        packed[i * 4 + 1] = int16_t(5000 * std::sin(i * 0.2));
        packed[i * 4 + 2] = 12345;
        packed[i * 4 + 3] = 20000;
    }
    for (int i = 0; i < 100; ++i)
        input->capture(packed, 64, InputFrameSamples, InputMicChannel);
    assert(input->status().captureCount == 3200);
    for (int i = 0; i < 3200; ++i)
        assert(input->sample[i] == packed[(i % 64) * 4 + 1]);

    auto replace = std::make_unique<Engine>();
    replace->loadDemo();
    const uint16_t originalLeadCount = replace->session.tracks[0].count;
    const uint16_t originalBassCount = replace->session.tracks[1].count;
    replace->setBpm(110);
    replace->setSwing(0);
    assert(replace->toggleRecord(true) == Result::Ok);
    assert(replace->session.tracks[0].count == originalLeadCount);
    replace->toggleRecord();
    assert(replace->session.tracks[0].count == originalLeadCount);
    assert(!replace->status().playing);
    replace->toggleRecord(true);
    replace->stop();
    assert(replace->session.tracks[0].count == originalLeadCount);
    replace->togglePlay();
    replace->toggleRecord(true);
    advance(*replace, 69000);
    assert(replace->status().armed);
    assert(replace->session.tracks[0].count == originalLeadCount);
    advance(*replace, 1000);
    assert(replace->status().preparation == RecordPreparation::GetReady);
    waitForRecording(*replace);
    assert(replace->status().recording);
    assert(replace->session.tracks[0].count == 0);
    assert(replace->session.tracks[1].count == originalBassCount);
    replace->noteOn(2);
    advance(*replace, 500);
    replace->noteOff();
    replace->stop();
    assert(replace->session.tracks[0].count == 1);
    replace->undo();
    assert(replace->session.tracks[0].count == originalLeadCount);
    replace->undo();
    assert(replace->session.tracks[0].count == 1);
    replace->toggleRecord(false);
    waitForRecording(*replace);
    assert(replace->status().recording);
    replace->noteOn(5);
    advance(*replace, 500);
    replace->noteOff();
    replace->stop();
    assert(replace->session.tracks[0].count == 2);
    replace->session.tracks[0].count = MaxNotes;
    assert(replace->toggleRecord(false) == Result::Full);
    assert(replace->toggleRecord(true) == Result::Ok);
    replace->stop();
    assert(replace->session.tracks[0].count == MaxNotes);

    for (int index = 0; index < DemoCount; ++index) {
        for (int track = 0; track < TrackCount; ++track) {
            auto demo = std::make_unique<Engine>();
            demo->session.root = 11;
            demo->session.scale = 2;
            demo->session.sampleCount = 1;
            for (Track &t : demo->session.tracks) { t.muted = true; t.tone = 0; t.patch = 3; }
            assert(demo->loadDemo(index) == Result::Ok);
            assert(Engine::valid(demo->session));
            assert(!demo->status().playing && !demo->status().recording && !demo->status().armed);
            assert(demo->session.sampleCount > 1600);
            for (int i = 0; i < TrackCount; ++i) {
                assert(demo->session.tracks[i].count > 0);
                assert(!demo->session.tracks[i].muted);
                demo->session.tracks[i].muted = i != track;
            }
            demo->togglePlay();
            uint32_t loopSamples = uint64_t(SampleRate) * 60 * 8 / demo->session.bpm;
            std::vector<int16_t> audio;
            speakerRms(*demo, loopSamples * 2, &audio);
            auto weighted = test::speakerSignal(audio.data(), audio.size());
            double rms = test::rms(weighted.data(), weighted.size());
            std::printf("DEMO %s track=%d measured_speaker_rms=%.1f\n", DemoLibrary[index].name, track, rms);
            std::fflush(stdout);
            assert(rms > 10);
        }
    }

    for (int index = 6; index < DemoCount; ++index) {
        auto demo = std::make_unique<Engine>();
        demo->loadDemo(index);
        for (int section = 0; section < SceneCount; ++section) {
            const Track &lead = demo->session.scenes[section].tracks[0];
            for (int other = section + 1; other < SceneCount; ++other) {
                const Track &comparison = demo->session.scenes[other].tracks[0];
                assert(lead.count != comparison.count ||
                    std::memcmp(lead.notes, comparison.notes, lead.count * sizeof(Note)) != 0);
            }
            demo->queueScene(section);
            assert(Engine::valid(demo->session));
            demo->togglePlay();
            uint32_t length = uint64_t(SampleRate) * 60 * 8 / demo->session.bpm;
            std::vector<int16_t> audio(length);
            demo->render(audio.data(), audio.size());
            size_t clipped = std::count_if(audio.begin(), audio.end(),
                [](int16_t value) { return std::abs(int(value)) >= 29999; });
            assert(clipped < audio.size() / 1000);
            demo->stop();
        }
    }

    auto mix = std::make_unique<Engine>();
    uint32_t previousHash = 0;
    for (int index = 0; index < DemoCount; ++index) {
        mix->setVolume(47);
        assert(mix->loadDemo(index) == Result::Ok);
        assert(mix->session.volume == 47);
        assert(!mix->status().playing);
        mix->togglePlay();
        uint32_t revision = mix->revision();
        assert(mix->loadDemo(-1) == Result::Invalid);
        assert(mix->loadDemo(DemoCount) == Result::Invalid);
        assert(mix->status().playing && mix->revision() == revision);
        int clipped = 0, peak = 0;
        uint32_t hash = 2166136261u;
        int16_t block[64];
        for (int i = 0; i < 4096; ++i) {
            mix->render(block, 64);
            for (int16_t value : block) {
                peak = std::max(peak, std::abs(int(value)));
                if (std::abs(int(value)) >= 29999) ++clipped;
                hash = (hash ^ uint16_t(value)) * 16777619u;
            }
        }
        std::printf("DEMO %s mix_peak=%d hard_limited_samples=%d/262144\n",
            DemoLibrary[index].name, peak, clipped);
        assert(clipped < 262);
        assert(hash != previousHash);
        previousHash = hash;
    }
    mix->startCapture();
    assert(mix->loadDemo(0) == Result::Busy);
    assert(mix->status().capturing);
    auto sections = std::make_unique<Engine>();
    sections->loadDemo();
    assert(sections->status().scene == 3 && sections->status().nextScene == -1);
    assert(sections->session.scenes[0].tracks[0].muted);
    assert(!sections->session.scenes[0].tracks[2].muted);
    sections->setPatch(29);
    sections->setPitch(7);
    uint16_t dropNotes = sections->session.tracks[0].count;
    assert(sections->queueScene(-1) == Result::Invalid);
    assert(sections->copyScene(4) == Result::Invalid);
    assert(sections->queueScene(0) == Result::Ok);
    assert(sections->status().scene == 0 && sections->session.tracks[0].muted);
    sections->clearSelected();
    sections->setPatch(7);
    assert(sections->queueScene(3) == Result::Ok);
    assert(sections->session.tracks[0].count == dropNotes);
    assert(sections->session.tracks[0].patch == 29 && sections->session.tracks[0].pitch == 7);
    sections->togglePlay();
    advance(*sections, 200);
    assert(sections->queueScene(0) == Result::Ok);
    assert(sections->status().scene == 3 && sections->status().nextScene == 0);
    uint32_t loopLength = uint64_t(SampleRate) * 60 * 8 / sections->session.bpm;
    advance(*sections, loopLength - 400);
    assert(sections->status().scene == 3);
    advance(*sections, 400);
    assert(sections->status().scene == 0 && sections->status().nextScene == -1);
    assert(sections->session.tracks[0].count == 0 && sections->session.tracks[0].patch == 7);
    assert(sections->queueScene(1) == Result::Ok);
    assert(sections->queueScene(1) == Result::Ok && sections->status().nextScene == -1);
    sections->queueScene(2);
    sections->stop();
    assert(sections->status().scene == 0 && sections->status().nextScene == -1);
    assert(sections->copyScene(2) == Result::Ok);
    sections->setPatch(12);
    sections->queueScene(2);
    assert(sections->session.tracks[0].patch == 7);
    sections->queueScene(0);
    assert(sections->session.tracks[0].patch == 12);
    sections->toggleRecord();
    assert(sections->queueScene(3) == Result::Busy);
    assert(sections->copyScene(3) == Result::Busy);
    sections->stop();
    assert(Engine::valid(sections->session));
    if (!sections->session.tracks[0].muted) sections->mute(0);
    assert(sections->session.tracks[0].muted);
    sections->toggleRecord();
    assert(sections->session.tracks[0].muted);
    sections->toggleRecord();
    assert(sections->session.tracks[0].muted);
    sections->toggleRecord();
    waitForRecording(*sections);
    assert(!sections->session.tracks[0].muted);
    sections->stop();
    sections->undo();
    assert(sections->session.tracks[0].muted);

    auto pieces = std::make_unique<Engine>();
    pieces->select(3);
    pieces->setSpace(0);
    assert(pieces->setSlices(true) == Result::NoSample);
    pieces->session.sampleCount = SampleCapacity;
    for (int part = 0; part < 8; ++part)
        for (int i = 0; i < SampleCapacity / 8; ++i)
            pieces->sample[part * (SampleCapacity / 8) + i] = int16_t(20000 *
                std::sin(i * 2 * 3.14159265358979323846 * (400 + part * 200) / SampleRate));
    std::vector<int16_t> originalSample(pieces->sample, pieces->sample + SampleCapacity);
    assert(pieces->setSlices(true) == Result::Ok);
    for (int pad = 0; pad < 8; ++pad) {
        pieces->noteOn(pad);
        advance(*pieces, 128);
        pieces->noteOff();
        std::vector<int16_t> audio(2048);
        pieces->render(audio.data(), audio.size());
        assert(frequencyShare(audio, 400 + pad * 200) > 0.1);
        assert(pieces->status().activePads[3] & (1 << pad));
        advance(*pieces, SampleRate / 2);
        assert(pieces->idle());
    }
    pieces->toggleRecord();
    waitForRecording(*pieces);
    assert(pieces->status().recording);
    pieces->noteOn(5);
    advance(*pieces, 512);
    pieces->noteOff();
    pieces->stop();
    assert(pieces->session.tracks[3].count == 1);
    assert(pieces->session.tracks[3].notes[0].pad == 133);
    assert(pieces->setSlices(false) == Result::Ok);
    assert(pieces->session.tracks[3].notes[0].pad == 133);
    pieces->noteOn(0);
    advance(*pieces, 512);
    pieces->noteOff();
    advance(*pieces, 5000);
    assert(pieces->status().activePads[3] & 1);
    pieces->stop();
    advance(*pieces, SampleRate);
    pieces->session.tracks[3].notes[0].tick = 0;
    pieces->togglePlay();
    advance(*pieces, 128);
    std::vector<int16_t> cutLoop(2048);
    pieces->render(cutLoop.data(), cutLoop.size());
    assert(frequencyShare(cutLoop, 1400) > 0.1);
    assert(std::equal(originalSample.begin(), originalSample.end(), pieces->sample));
    assert(Engine::valid(pieces->session));
    auto invalidPiece = std::make_unique<Session>(pieces->session);
    invalidPiece->tracks[0].count = 1;
    invalidPiece->tracks[0].notes[0] = Note{0, 24, 128, 100};
    assert(!Engine::valid(*invalidPiece));
    invalidPiece->tracks[0].notes[0].pad = 0;
    invalidPiece->tracks[3].notes[0].pad = 136;
    assert(!Engine::valid(*invalidPiece));

    auto moves = std::make_unique<Engine>();
    moves->loadDemo();
    assert(moves->touchFx(-1, 50) == Result::Invalid);
    assert(moves->touchFx(50, 101) == Result::Invalid);
    uint32_t untouchedRevision = moves->revision();
    assert(moves->touchFx(0, 100) == Result::Ok);
    assert(moves->status().effect.active && moves->status().effect.x == 0);
    assert(moves->revision() == untouchedRevision);
    moves->releaseFx();
    assert(!moves->status().effect.active && moves->revision() == untouchedRevision);
    assert(moves->recordFx() == Result::Ok && moves->status().fxArmed);
    assert(moves->toggleRecord() == Result::Busy);
    assert(moves->queueScene(0) == Result::Busy);
    auto beforePracticeMoves = moves->session.fx;
    advance(*moves, (uint64_t(SampleRate) * 60 * 16 + moves->session.bpm - 1) / moves->session.bpm);
    assert(moves->status().preparation == RecordPreparation::Practice);
    moves->touchFx(20, 85);
    advance(*moves, 512);
    moves->releaseFx();
    assert(std::memcmp(&beforePracticeMoves, &moves->session.fx, sizeof(FxLoop)) == 0);
    assert(moves->recordFx() == Result::Ok);
    assert(!moves->status().playing && !moves->status().fxArmed);
    moves->recordFx();
    waitForRecording(*moves);
    assert(moves->status().fxRecording && !moves->status().fxArmed);
    auto notesBeforeMoves = std::make_unique<Session>(moves->session);
    assert(moves->touchFx(20, 85) == Result::Ok);
    advance(*moves, 4000);
    moves->releaseFx();
    uint32_t musicLoop = uint64_t(SampleRate) * 60 * 8 / moves->session.bpm;
    advance(*moves, musicLoop);
    assert(!moves->status().fxRecording && moves->status().fxEnabled);
    bool hasMovement = false, hasRelease = false;
    for (const auto &point : moves->session.fx.points) {
        hasMovement |= point.active && point.x == 20 && point.y == 85;
        hasRelease |= !point.active;
    }
    assert(hasMovement && hasRelease);
    assert(std::memcmp(notesBeforeMoves->tracks, moves->session.tracks, sizeof(moves->session.tracks)) == 0);
    assert(moves->clearFx() == Result::Ok);
    assert(!moves->status().fxEnabled);
    assert(moves->undoFx() == Result::Ok && moves->status().fxEnabled);
    assert(moves->enableFx(false) == Result::Ok);
    assert(!moves->status().effect.active);
    moves->stop();
    assert(!moves->status().fxTouched && !moves->status().fxRecording);
    moves->enableFx(true);
    moves->queueScene(0);
    assert(!moves->session.fx.enabled);
    moves->queueScene(3);
    assert(moves->session.fx.enabled);
    assert(Engine::valid(moves->session));
    moves->startCapture();
    assert(moves->touchFx(20, 85) == Result::Busy);
    assert(moves->recordFx() == Result::Busy);
    assert(moves->queueScene(0) == Result::Busy);

    auto blankMoves = std::make_unique<Engine>();
    blankMoves->recordFx();
    waitForRecording(*blankMoves);
    advance(*blankMoves, SampleRate * 8);
    assert(!blankMoves->status().fxRecording && !blankMoves->status().fxEnabled);

    auto dry = std::make_unique<Engine>();
    dry->setPatch(18);
    dry->setSpace(0);
    dry->noteOn(3);
    advance(*dry, 1000);
    auto filtered = std::make_unique<Engine>(*dry);
    auto cleanFx = std::make_unique<Engine>(*dry);
    filtered->touchFx(0, 0);
    cleanFx->touchFx(100, 0);
    advance(*filtered, 1000);
    advance(*cleanFx, 1000);
    advance(*dry, 1000);
    double softRms = speakerRms(*filtered, 4000);
    double brightRms = speakerRms(*cleanFx, 4000);
    double dryRms = speakerRms(*dry, 4000);
    assert(softRms < brightRms * 0.65);
    assert(std::abs(brightRms - dryRms) < dryRms * 0.02);
    filtered->releaseFx();
    advance(*filtered, SampleRate);
    advance(*dry, SampleRate);
    assert(std::abs(speakerRms(*filtered, 4000) - speakerRms(*dry, 4000)) < dryRms * 0.05);
    filtered->touchFx(100, 100);
    filtered->noteOff();
    advance(*filtered, SampleRate * 3);
    filtered->releaseFx();
    advance(*filtered, SampleRate);
    assert(filtered->idle());

    auto migration = std::make_unique<Session>();
    std::copy_n(reinterpret_cast<const uint8_t *>(&sections->session), LegacySessionBytes,
        reinterpret_cast<uint8_t *>(migration.get()));
    migration->version = 2;
    Engine::upgradeSession(*migration, 2);
    assert(Engine::valid(*migration));
    for (int track = 0; track < TrackCount; ++track) {
        assert(std::memcmp(&migration->tracks[track], &sections->session.tracks[track],
                           offsetof(Track, reserved)) == 0);
        assert(migration->tracks[track].reserved == 0);
    }
    auto savedPerformance = std::make_unique<Session>(sections->session);
    assert(Engine::valid(*savedPerformance));
    assert(savedPerformance->scenes[0].tracks[0].patch == 12);
    savedPerformance->scenes[0].fx.points[2].x = 101;
    assert(!Engine::valid(*savedPerformance));
    std::puts("PASS: scenes, slices, looped FX, legacy migration and all audio regressions");
}
