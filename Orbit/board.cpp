#include "board.h"

#include <Wire.h>
#include <atomic>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "es8311.h"
#include "engine.h"
#include "audio_input.h"

namespace board {
i2s_chan_handle_t tx = nullptr;
i2s_chan_handle_t rx = nullptr;
namespace {
XPowersPMU power;
const char *lastError = "";
bool micOnline = false;
es8311_handle_t codec = nullptr;
bool txRunning = false, rxRunning = false, touchAutoSleep = false;
std::atomic<bool> touchInterrupt{false};
std::atomic<uint32_t> droppedFrames{0};
void IRAM_ATTR onTouch() { touchInterrupt.store(true, std::memory_order_relaxed); }
bool IRAM_ATTR onRxOverflow(i2s_chan_handle_t, i2s_event_data_t *, void *) {
    droppedFrames.fetch_add(1, std::memory_order_relaxed);
    return false;
}
bool write(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    uint8_t result = Wire.endTransmission();
    if (result) Serial.printf("I2C_ERROR addr=%02x reg=%02x result=%u\n", address, reg, result);
    return result == 0;
}
bool check(esp_err_t result, const char *context) {
    if (result == ESP_OK) return true;
    Serial.printf("HARDWARE_ERROR %s: %s\n", context, esp_err_to_name(result));
    lastError = context;
    return false;
}

bool setChannel(i2s_chan_handle_t channel, bool &running, bool enabled) {
    if (running == enabled) return true;
    if (!check(enabled ? i2s_channel_enable(channel) : i2s_channel_disable(channel),
        enabled ? "I2S resume" : "I2S suspend")) return false;
    running = enabled;
    return true;
}

bool initSpeaker() {
    const es8311_clock_config_t clock{false, false, true, orbit::SampleRate * 256, orbit::SampleRate};
    return check(es8311_init(codec, &clock, ES8311_RESOLUTION_32, ES8311_RESOLUTION_32), "Speaker codec") &&
        check(es8311_voice_volume_set(codec, 80, nullptr), "Speaker codec volume") &&
        check(es8311_voice_mute(codec, false), "Speaker codec unmute");
}

bool initMic() {
    // ES7210: four 16-bit TDM slots share the board's two 32-bit I2S slots.
    // Register sequence follows Espressif's Apache-2.0 ES7210 codec driver.
    // Keep MODE_CONFIG's LRCK-rate field at 0x10 when selecting slave mode:
    // clearing it inserts 14 silent frames for every two real frames on this board.
    constexpr uint8_t registers[][2] = {
        {0x00, 0xff}, {0x00, 0x41}, {0x01, 0x3f},
        {0x09, 0x30}, {0x0a, 0x30}, {0x23, 0x2a}, {0x22, 0x0a},
        {0x20, 0x0a}, {0x21, 0x2a}, {0x08, 0x10},
        {0x40, 0x43}, {0x41, 0x70}, {0x42, 0x70},
        {0x07, 0x20}, {0x02, 0xc1},
        {0x43, 0x1a}, {0x44, 0x1a}, {0x45, 0x1a}, {0x46, 0x1a},
        {0x4b, 0x00}, {0x4c, 0x00},
        {0x11, 0x60}, {0x12, 0x02}, {0x14, 0x00}, {0x15, 0x00},
        {0x01, 0x00}, {0x06, 0x00}, {0x40, 0x43},
        {0x47, 0x08}, {0x48, 0x08}, {0x49, 0x08}, {0x4a, 0x08},
        {0x00, 0x71}, {0x00, 0x41}
    };
    for (const auto &reg : registers) if (!write(0x40, reg[0], reg[1])) return false;
    return true;
}
}

bool begin() {
    pinMode(0, OUTPUT);
    amplifier(false);
    pinMode(9, INPUT_PULLUP);
    pinMode(11, INPUT_PULLUP);
    attachInterrupt(11, onTouch, FALLING);
    if (!check(gpio_wakeup_enable(GPIO_NUM_9, GPIO_INTR_LOW_LEVEL), "BOOT wake") ||
        !check(esp_sleep_enable_gpio_wakeup(), "GPIO wake")) return false;
    if (!Wire.begin(7, 8, 400000)) { lastError = "I2C bus"; return false; }
    Wire.setTimeOut(15);
    if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, 7, 8)) {
        lastError = "Power controller"; return false;
    }
    // Only the two documented 3.3 V peripheral rails are touched.
    power.setALDO1Voltage(3300);
    power.setALDO2Voltage(3300);
    power.enableALDO1();
    power.enableALDO2();
    power.disableWatchdog();
    power.clearIrqStatus();
    power.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    if (!write(0x15, 0xfe, 0xff) || !write(0x15, 0xfa, 0x60)) {
        lastError = "Touch controller"; return false;
    }

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = 64;
    channel.auto_clear = true;
    if (!check(i2s_new_channel(&channel, &tx, &rx), "I2S channels")) return false;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(orbit::SampleRate);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_19;
    config.gpio_cfg.bclk = GPIO_NUM_20;
    config.gpio_cfg.ws = GPIO_NUM_22;
    config.gpio_cfg.dout = GPIO_NUM_23;
    config.gpio_cfg.din = GPIO_NUM_21;
    if (!check(i2s_channel_init_std_mode(tx, &config), "I2S output format") ||
        !check(i2s_channel_init_std_mode(rx, &config), "I2S input format")) return false;
    i2s_event_callbacks_t callbacks{};
    callbacks.on_recv_q_ovf = onRxOverflow;
    if (!check(i2s_channel_register_event_callback(rx, &callbacks, nullptr), "I2S input diagnostics"))
        return false;

    codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
    if (!codec) { lastError = "Speaker codec allocation"; return false; }
    if (!initSpeaker()) return false;
    micOnline = initMic();
    if (!micOnline) Serial.println("MIC_ERROR sampler unavailable; synth still usable");
    if (!setChannel(tx, txRunning, true) || !setChannel(rx, rxRunning, true)) return false;
    amplifier(true);
    return true;
}

bool readTouch(int &x, int &y, bool &down) {
    if (touchAutoSleep) {
        // Standby touch may not answer I2C until its interrupt announces a touch.
        if (!touchWakePending()) { down = false; return true; }
        if (!write(0x15, 0xfe, 0xff)) return false;
        touchAutoSleep = false;
    }
    touchInterrupt.exchange(false, std::memory_order_relaxed);
    Wire.beginTransmission(0x15);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(uint8_t(0x15), uint8_t(5)) != 5) return false;
    uint8_t count = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    x = ((xh & 15) << 8) | xl;
    y = ((yh & 15) << 8) | yl;
    down = (count & 15) != 0 && x < Width && y < Height;
    return true;
}

bool touchWakePending() {
    return touchInterrupt.load(std::memory_order_relaxed) || digitalRead(11) == LOW;
}

esp_err_t lightSleep() {
    // GPIO wake shares the interrupt type with the ordinary edge ISR. Mask that
    // ISR while using level wake, then restore edges to avoid an interrupt storm.
    if (!check(gpio_intr_disable(GPIO_NUM_11), "Touch interrupt suspend")) return ESP_FAIL;
    esp_err_t result = gpio_wakeup_enable(GPIO_NUM_11, GPIO_INTR_LOW_LEVEL);
    if (result == ESP_OK)
        result = touchWakePending() ? ESP_ERR_SLEEP_REJECT : esp_light_sleep_start();
    bool restored = check(gpio_wakeup_disable(GPIO_NUM_11), "Touch wake disable");
    restored = check(gpio_set_intr_type(GPIO_NUM_11, GPIO_INTR_NEGEDGE), "Touch interrupt edge") && restored;
    restored = check(gpio_intr_enable(GPIO_NUM_11), "Touch interrupt resume") && restored;
    return restored ? result : ESP_FAIL;
}

bool suspendAudio() {
    amplifier(false);
    if (!touchAutoSleep) {
        if (!write(0x15, 0xfe, 0x00)) return false;
        touchAutoSleep = true;
    }
    // Espressif esp_codec_dev standby sequences; retain the shared peripheral rails.
    constexpr uint8_t speakerOff[][2] = {
        {0x32, 0x00}, {0x17, 0x00}, {0x0e, 0xff}, {0x12, 0x02},
        {0x14, 0x00}, {0x0d, 0xfa}, {0x15, 0x00}, {0x02, 0x10},
        {0x00, 0x00}, {0x00, 0x1f}, {0x01, 0x30}, {0x01, 0x00},
        {0x45, 0x00}, {0x0d, 0xfc}, {0x02, 0x00}
    };
    constexpr uint8_t micOff[][2] = {
        {0x47, 0xff}, {0x48, 0xff}, {0x49, 0xff}, {0x4a, 0xff},
        {0x4b, 0xff}, {0x4c, 0xff}, {0x40, 0xc0}, {0x01, 0x7f}, {0x06, 0x07}
    };
    for (const auto &reg : speakerOff) if (!write(0x18, reg[0], reg[1])) return false;
    if (micOnline)
        for (const auto &reg : micOff) if (!write(0x40, reg[0], reg[1])) return false;
    return setChannel(rx, rxRunning, false) && setChannel(tx, txRunning, false);
}

bool resumeAudio() {
    amplifier(false);
    if (!setChannel(rx, rxRunning, false) || !setChannel(tx, txRunning, false) ||
        !initSpeaker()) return false;
    if (micOnline && !initMic()) {
        lastError = "Microphone resume";
        Serial.println("HARDWARE_ERROR microphone resume");
        return false;
    }
    if (!setChannel(tx, txRunning, true) || !setChannel(rx, rxRunning, true)) return false;
    // Settle clocks and microphone input with the amplifier still physically off.
    int32_t silence[128]{};
    int16_t input[64 * orbit::InputFrameSamples];
    for (int i = 0; i < 50; ++i) {
        size_t written = 0, bytes = 0;
        if (!check(i2s_channel_write(tx, silence, sizeof(silence), &written, 100), "I2S wake output") ||
            !check(i2s_channel_read(rx, input, sizeof(input), &bytes, 100), "I2S wake input")) return false;
        if (written != sizeof(silence) || bytes != sizeof(input)) {
            lastError = "Short audio wake transfer";
            Serial.printf("HARDWARE_ERROR audio wake bytes=%u,%u\n", unsigned(written), unsigned(bytes));
            return false;
        }
    }
    return true;
}

bool powerPressed() {
    power.getIrqStatus();
    bool pressed = power.isPekeyShortPressIrq();
    power.clearIrqStatus();
    return pressed;
}
BatteryStatus readBattery() {
    BatteryStatus battery;
    uint8_t flags[2]{};
    if (power.readRegister(XPOWERS_AXP2101_STATUS1, flags, sizeof(flags)) < 0) {
        Serial.println("BATTERY_ERROR cannot read power status");
        return battery;
    }
    battery.valid = true;
    battery.connected = (flags[0] & (1 << 3)) != 0;
    battery.usb = (flags[0] & (1 << 5)) && !(flags[1] & (1 << 3));
    battery.charging = battery.connected && (flags[1] >> 5) == 1;
    if (battery.connected) {
        int percent = power.readRegister(XPOWERS_AXP2101_BAT_PERCENT_DATA);
        if (percent >= 0 && percent <= 100) battery.percent = percent;
        else Serial.printf("BATTERY_ERROR invalid charge level %d\n", percent);
    }
    return battery;
}
void amplifier(bool enabled) { digitalWrite(0, enabled ? HIGH : LOW); }
bool micAvailable() { return micOnline; }
uint32_t rxDrops() { return droppedFrames.load(std::memory_order_relaxed); }
const char *error() { return lastError; }
}
