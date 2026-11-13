#pragma once

#include <algorithm>
#include <stddef.h>
#include <stdint.h>

namespace orbit {

struct PowerChimeAudio {
    const int16_t *samples;
    uint32_t frames;
};

extern const PowerChimeAudio StartupChime;
extern const PowerChimeAudio ShutdownChime;

class PowerChimePlayer {
public:
    void start(const PowerChimeAudio &audio, uint8_t volume) {
        audio_ = &audio;
        position_ = 0;
        volume_ = std::min<int>(volume, 35);
    }

    void stop() { audio_ = nullptr; }
    bool active() const { return audio_ != nullptr; }

    void render(int16_t *out, size_t count) {
        size_t frames = audio_ ? std::min<size_t>(count, audio_->frames - position_) : 0;
        for (size_t i = 0; i < frames; ++i)
            out[i] = int32_t(audio_->samples[position_ + i]) * volume_ / 128;
        std::fill(out + frames, out + count, 0);
        position_ += frames;
        if (audio_ && position_ == audio_->frames) stop();
    }

private:
    const PowerChimeAudio *audio_ = nullptr;
    uint32_t position_ = 0;
    uint8_t volume_ = 0;
};

} // namespace orbit
