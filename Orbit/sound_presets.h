#pragma once

#include <stdint.h>

namespace orbit {
constexpr int SoundCount = 30;
constexpr int SoundFamilyCount = 8;

enum class SoundWave : uint8_t {
    Pluck, Warm, Bell, Acid, Guitar, Harp, Piano, Organ, Pad, Saw,
    Square, Flute, Sub, Rubber, Growl, Chip, Arcade, Choir, Space, Robot, Sample
};


struct SoundPreset {
    const char *name;
    uint8_t family;
    SoundWave wave;
    double ratio;
    uint16_t attack;
    uint8_t decayShift;
    uint8_t releaseShift;
    uint8_t brightness;
    uint8_t gain;
    uint8_t character;
    bool sweep;
};

extern const SoundPreset Sounds[SoundCount];
extern const char *const SoundFamilies[SoundFamilyCount];
int familySound(int family, int position);
}
