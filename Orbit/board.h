#pragma once

#include <Arduino.h>
#include "driver/i2s_std.h"

namespace board {
constexpr int Width = 240;
constexpr int Height = 284;
extern i2s_chan_handle_t tx;
extern i2s_chan_handle_t rx;
struct BatteryStatus {
    bool valid = false;
    bool connected = false;
    bool charging = false;
    bool usb = false;
    int percent = -1;
};
bool begin();
BatteryStatus readBattery();
bool readTouch(int &x, int &y, bool &down);
bool powerPressed();
void amplifier(bool enabled);
void powerOff();
bool micAvailable();
uint32_t rxDrops();
const char *error();
}
