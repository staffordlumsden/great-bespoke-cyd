#pragma once
#include <cstdint>

namespace ledapp {
struct Colour {
    uint8_t r = 0, g = 0, b = 0; // channel percentages
    bool operator==(const Colour &other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const Colour &other) const { return !(*this == other); }
};
enum class Mode : uint8_t { solid, flash, blue_red, green_yellow, green_white };
enum class Page { colours, patterns, mixer };
enum class Action {
    none,
    power,
    theme,
    console,
    colours,
    patterns,
    flashlight,
    mixer,
    back,
    red,
    green,
    blue,
    yellow,
    white,
    solid,
    flash,
    blue_red,
    green_yellow,
    green_white,
    speed,
    dimmer,
    brighter,
    red_down,
    red_up,
    green_down,
    green_up,
    blue_down,
    blue_up
};
struct State {
    bool enabled = false;
    bool light = false;
    unsigned brightness = 70;
    Colour colour{0, 0, 100};
    Mode mode = Mode::solid;
    unsigned speed = 1; // half-periods: 1000, 500, 250 ms
    uint64_t phase_start = 0;
    Page page = Page::colours;
};
void sanitize(State &state); // persisted data never restores power
Colour output(const State &state, uint64_t now_ms);
unsigned pwm_duty(uint8_t channel_percent, unsigned brightness);
bool apply(State &state, Action action, uint64_t now_ms);
const char *mode_name(Mode mode);
} // namespace ledapp
