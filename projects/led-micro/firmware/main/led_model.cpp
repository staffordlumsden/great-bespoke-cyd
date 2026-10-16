#include "led_model.h"
#include <algorithm>

namespace ledapp {
namespace {
uint8_t step(uint8_t value, int direction) {
    return static_cast<uint8_t>(std::clamp(int(value) + direction * 10, 0, 100));
}
} // namespace
void sanitize(State &s) {
    s.enabled = false;
    s.brightness = std::min(s.brightness, 100u);
    s.colour.r = std::min(s.colour.r, uint8_t(100));
    s.colour.g = std::min(s.colour.g, uint8_t(100));
    s.colour.b = std::min(s.colour.b, uint8_t(100));
    if (unsigned(s.mode) > unsigned(Mode::green_white))
        s.mode = Mode::solid;
    if (s.speed > 2)
        s.speed = 1;
    s.page = Page::colours;
    s.phase_start = 0;
}
Colour output(const State &s, uint64_t now) {
    if (!s.enabled || !s.brightness)
        return {};
    const unsigned intervals[] = {1000, 500, 250};
    const uint64_t elapsed = now >= s.phase_start ? now - s.phase_start : 0;
    const bool second = (elapsed / intervals[std::min(s.speed, 2u)]) % 2;
    switch (s.mode) {
    case Mode::flash:
        return second ? Colour{} : s.colour;
    case Mode::blue_red:
        return second ? Colour{100, 0, 0} : Colour{0, 0, 100};
    case Mode::green_yellow:
        return second ? Colour{100, 100, 0} : Colour{0, 100, 0};
    case Mode::green_white:
        return second ? Colour{100, 100, 100} : Colour{0, 100, 0};
    default:
        return s.colour;
    }
}
unsigned pwm_duty(uint8_t channel, unsigned brightness) {
    return (unsigned(std::min(channel, uint8_t(100))) * std::min(brightness, 100u) * 4095u +
            5000u) /
           10000u;
}
const char *mode_name(Mode mode) {
    switch (mode) {
    case Mode::flash:
        return "FLASHING";
    case Mode::blue_red:
        return "BLUE / RED";
    case Mode::green_yellow:
        return "GREEN / YELLOW";
    case Mode::green_white:
        return "GREEN / WHITE";
    default:
        return "SOLID";
    }
}
bool apply(State &s, Action a, uint64_t now) {
    bool reset_phase = true;
    switch (a) {
    case Action::power:
        s.enabled = !s.enabled;
        break;
    case Action::theme:
        s.light = !s.light;
        reset_phase = false;
        break;
    case Action::colours:
    case Action::back:
        s.page = Page::colours;
        reset_phase = false;
        break;
    case Action::patterns:
        s.page = Page::patterns;
        reset_phase = false;
        break;
    case Action::mixer:
        s.page = Page::mixer;
        s.mode = Mode::solid;
        break;
    case Action::flashlight:
        s.enabled = true;
        s.colour = {100, 100, 100};
        s.brightness = 100;
        s.mode = Mode::solid;
        s.page = Page::colours;
        break;
    case Action::red:
        s.colour = {100, 0, 0};
        s.mode = Mode::solid;
        break;
    case Action::green:
        s.colour = {0, 100, 0};
        s.mode = Mode::solid;
        break;
    case Action::blue:
        s.colour = {0, 0, 100};
        s.mode = Mode::solid;
        break;
    case Action::yellow:
        s.colour = {100, 100, 0};
        s.mode = Mode::solid;
        break;
    case Action::white:
        s.colour = {100, 100, 100};
        s.mode = Mode::solid;
        break;
    case Action::solid:
        s.mode = Mode::solid;
        break;
    case Action::flash:
        s.mode = Mode::flash;
        break;
    case Action::blue_red:
        s.mode = Mode::blue_red;
        break;
    case Action::green_yellow:
        s.mode = Mode::green_yellow;
        break;
    case Action::green_white:
        s.mode = Mode::green_white;
        break;
    case Action::speed:
        s.speed = (s.speed + 1) % 3;
        break;
    case Action::dimmer:
        s.brightness = s.brightness >= 10 ? s.brightness - 10 : 0;
        reset_phase = false;
        break;
    case Action::brighter:
        s.brightness = std::min(s.brightness + 10, 100u);
        reset_phase = false;
        break;
    case Action::red_down:
        s.colour.r = step(s.colour.r, -1);
        break;
    case Action::red_up:
        s.colour.r = step(s.colour.r, 1);
        break;
    case Action::green_down:
        s.colour.g = step(s.colour.g, -1);
        break;
    case Action::green_up:
        s.colour.g = step(s.colour.g, 1);
        break;
    case Action::blue_down:
        s.colour.b = step(s.colour.b, -1);
        break;
    case Action::blue_up:
        s.colour.b = step(s.colour.b, 1);
        break;
    default:
        return false;
    }
    if (reset_phase)
        s.phase_start = now;
    return true;
}
} // namespace ledapp
