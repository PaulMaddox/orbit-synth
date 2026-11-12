#pragma once

#include <stdint.h>

class IdlePowerOff {
public:
    static constexpr uint32_t TimeoutMs = 45000;
    static constexpr uint32_t WarningMs = 15000;

    void activity(uint32_t now) { lastActivity_ = now; }

    void update(uint32_t now, bool busy) {
        if (busy) activity(now);
    }

    bool due(uint32_t now) const { return idleMs(now) >= TimeoutMs; }
    uint32_t countdown(uint32_t now) const {
        uint32_t elapsed = idleMs(now);
        if (elapsed < TimeoutMs - WarningMs || elapsed >= TimeoutMs) return 0;
        return (TimeoutMs - elapsed + 999) / 1000;
    }
    uint32_t idleMs(uint32_t now) const { return now - lastActivity_; }

private:
    uint32_t lastActivity_ = 0;
};
