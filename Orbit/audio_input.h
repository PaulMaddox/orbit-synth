#pragma once

#include <stddef.h>
#include <stdint.h>

namespace orbit {
// Waveshare's packed ES7210 input format is RMNM: reference, mic, unused, mic.
constexpr size_t InputFrameSamples = 4;
constexpr int InputMicChannel = 1;
constexpr size_t InputFrameBytes = InputFrameSamples * sizeof(int16_t);
}
