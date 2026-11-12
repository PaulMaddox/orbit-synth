#include "sample_bank.h"
#include <cstdlib>

namespace orbit {
int instrumentMidi(int patch, int midi) {
    // Practical recorded registers: retain the scale, never fake a sub-bass flute.
    constexpr uint8_t ranges[30][2] = {
        {40,88}, {0,127}, {72,100}, {0,127}, {40,88}, {48,96},
        {60,96}, {36,96}, {40,88}, {36,96}, {49,84}, {54,86},
        {41,77}, {40,72}, {36,84}, {55,96}, {0,127}, {0,127},
        {0,127}, {60,96}, {28,67}, {0,127}, {28,67}, {0,127},
        {50,94}, {58,91}, {0,127}, {0,127}, {28,65}, {55,96}
    };
    if (patch < 0 || patch >= 30) return midi;
    while (midi < ranges[patch][0]) midi += 12;
    while (midi > ranges[patch][1]) midi -= 12;
    return midi;
}

const InstrumentSample *findInstrumentSample(int patch, int midi, int velocity) {
    const InstrumentSample *best = nullptr;
    int distance = 10000;
    for (size_t i = 0; i < InstrumentSampleCount; ++i) {
        const InstrumentSample &sample = InstrumentSamples[i];
        if (sample.patch != patch) continue;
        int score = std::abs(midi - sample.root) +
            (sample.layer != (velocity > 90) ? 128 : 0);
        if (score < distance) { best = &sample; distance = score; }
    }
    return best;
}
}
