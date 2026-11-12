#pragma once

#include <stdint.h>

namespace ui {
class TouchGesture {
public:
    void begin(int x, int y, uint32_t now) {
        startX_ = lastX_ = x;
        startY_ = lastY_ = y;
        started_ = now;
        consumed_ = false;
    }

    void move(int x, int y) {
        lastX_ = x;
        lastY_ = y;
    }

    bool takeHold(uint32_t now, bool eligible) {
        if (!eligible || consumed_ || now - started_ <= 650 || !within(14)) return false;
        consumed_ = true;
        return true;
    }

    bool tapped() const { return !consumed_ && within(24); }
    int x() const { return startX_; }
    int y() const { return startY_; }

private:
    int startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
    uint32_t started_ = 0;
    bool consumed_ = false;

    bool within(int distance) const {
        int dx = lastX_ - startX_, dy = lastY_ - startY_;
        return dx > -distance && dx < distance && dy > -distance && dy < distance;
    }
};
} // namespace ui
