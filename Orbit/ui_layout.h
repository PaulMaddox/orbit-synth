#pragma once

#include <stddef.h>

namespace ui {
struct Rect {
    int x, y, w, h;
    constexpr bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

constexpr int HeaderLeft = 24;
constexpr int HeaderRight = 216;
constexpr Rect HeaderStatus{134, 7, 83, 26};
constexpr Rect Back{170, 8, 46, 26};
constexpr Rect BackTouch{166, 8, 50, 26};
constexpr Rect Footer[] = {{20, 237, 62, 31}, {89, 237, 62, 31}, {158, 237, 62, 31}};
constexpr Rect FooterPair[] = {{20, 237, 96, 31}, {124, 237, 96, 31}};
constexpr Rect FooterWide{20, 237, 200, 31};
constexpr Rect SoundPrevious{8, 42, 42, 29};
constexpr Rect SoundNext{190, 42, 42, 29};
constexpr Rect SoundCards[] = {{8, 94, 108, 54}, {124, 94, 108, 54},
                              {8, 156, 108, 54}, {124, 156, 108, 54}};
constexpr Rect Navigation{24, 8, 109, 26};
constexpr Rect MenuCards[] = {{8, 44, 108, 50}, {124, 44, 108, 50},
                             {8, 102, 108, 50}, {124, 102, 108, 50},
                             {8, 160, 108, 50}, {124, 160, 108, 50}};
constexpr Rect SceneCards[] = {{8, 47, 108, 66}, {124, 47, 108, 66},
                              {8, 121, 108, 66}, {124, 121, 108, 66}};
constexpr Rect SceneCopy{20, 195, 200, 28};
constexpr Rect FxPad{20, 73, 200, 135};
constexpr Rect FxOptions[] = {{8, 44, 224, 45}, {8, 98, 224, 45},
                             {8, 152, 224, 45}};
constexpr Rect SliceWhole{8, 82, 108, 24};
constexpr Rect SliceCut{124, 82, 108, 24};

constexpr Rect slicePad(int pad) {
    return {8 + (pad % 4) * 57, pad < 4 ? 168 : 112, 53, 49};
}

inline int sliceAt(int x, int y) {
    for (int pad = 0; pad < 8; ++pad) if (slicePad(pad).contains(x, y)) return pad;
    return -1;
}

template <size_t N>
int hit(const Rect (&controls)[N], int x, int y) {
    for (size_t i = 0; i < N; ++i) if (controls[i].contains(x, y)) return int(i);
    return -1;
}
}
