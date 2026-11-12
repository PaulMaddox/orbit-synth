#pragma once

#include "engine.h"

namespace orbit {
struct DemoVoice {
    const int16_t *samples;
    uint16_t count;
};
extern const DemoVoice DemoVoices[DemoCount];
}
