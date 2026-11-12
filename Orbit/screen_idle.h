#pragma once

#include <stdint.h>

class ScreenIdle {
public:
    static constexpr uint32_t TimeoutMs = 60000;

    bool activity(uint32_t now) {
        bool woke = sleeping_;
        sleeping_ = false;
        lastActivity_ = now;
        return woke;
    }

    bool sleepIfIdle(uint32_t now, bool busy = false) {
        if (busy) {
            lastActivity_ = now;
            return false;
        }
        if (sleeping_ || idleMs(now) < TimeoutMs) return false;
        sleeping_ = true;
        return true;
    }

    bool sleeping() const { return sleeping_; }
    uint32_t idleMs(uint32_t now) const { return now - lastActivity_; }

private:
    uint32_t lastActivity_ = 0;
    bool sleeping_ = false;
};
