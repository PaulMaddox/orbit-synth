#pragma once

#include <stddef.h>
#include <stdint.h>

namespace orbit {
struct InstrumentSample {
    const uint8_t *data;
    uint32_t frames;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint8_t patch;
    uint8_t root;
    uint8_t layer;
};
extern const uint8_t InstrumentBank[];
extern const size_t InstrumentBankBytes;
extern const InstrumentSample InstrumentSamples[];
extern const size_t InstrumentSampleCount;
extern const int16_t InstrumentDecode[256];
extern const int16_t InstrumentResampler[8][32][16];
constexpr uint16_t InstrumentCutoffs[8] = {61604, 49152, 39322, 30802, 22938, 15401, 7700, 3850};

const InstrumentSample *findInstrumentSample(int patch, int midi, int velocity);
int instrumentMidi(int patch, int midi);
}
