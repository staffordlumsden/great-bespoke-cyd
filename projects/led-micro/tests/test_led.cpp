#include "led_model.h"
#include "touch.h"
#include "ui.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace ledapp;
void raster(const View &v, const std::string &name) {
    prepare(v);
    assert(scene_valid());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            auto p = pixel(x, y);
            unsigned char rgb[] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                                   static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                                   static_cast<unsigned char>((p & 31) * 255 / 31)};
            out.write(reinterpret_cast<const char *>(rgb), 3);
        }
    assert(out.good());
}
int main(int argc, char **argv) {
    assert(argc == 2);
    State s;
    assert(output(s, 0) == Colour{});
    apply(s, Action::power, 100);
    assert((output(s, 100) == Colour{0, 0, 100}));
    apply(s, Action::flash, 100);
    assert((output(s, 599) == Colour{0, 0, 100}));
    assert(output(s, 600) == Colour{});
    assert((output(s, 1100) == Colour{0, 0, 100}));
    apply(s, Action::blue_red, 2000);
    assert((output(s, 2000) == Colour{0, 0, 100}));
    assert((output(s, 2500) == Colour{100, 0, 0}));
    assert((output(s, 3000) == Colour{0, 0, 100}));
    apply(s, Action::green_yellow, 4000);
    assert((output(s, 4000) == Colour{0, 100, 0}));
    assert((output(s, 4500) == Colour{100, 100, 0}));
    apply(s, Action::green_white, 5000);
    assert((output(s, 5500) == Colour{100, 100, 100}));
    apply(s, Action::speed, 6000);
    assert(s.speed == 2);
    assert((output(s, 6250) == Colour{100, 100, 100}));
    apply(s, Action::speed, 7000);
    assert(s.speed == 0);
    assert((output(s, 7999) == Colour{0, 100, 0}));
    apply(s, Action::power, 8000);
    assert(output(s, 8500) == Colour{});
    apply(s, Action::flashlight, 9000);
    assert(s.enabled && s.brightness == 100 && s.mode == Mode::solid);
    assert((output(s, 9999999) == Colour{100, 100, 100}));
    for (int i = 0; i < 20; ++i)
        apply(s, Action::dimmer, 0);
    assert(s.brightness == 0 && output(s, 0) == Colour{});
    for (int i = 0; i < 20; ++i)
        apply(s, Action::brighter, 0);
    assert(s.brightness == 100);
    apply(s, Action::mixer, 0);
    for (int i = 0; i < 20; ++i)
        apply(s, Action::red_down, 0);
    assert(s.colour.r == 0);
    for (int i = 0; i < 20; ++i)
        apply(s, Action::red_up, 0);
    assert(s.colour.r == 100);
    for (unsigned b = 0; b <= 120; ++b)
        for (unsigned c = 0; c <= 255; ++c)
            assert(pwm_duty(c, b) <= 4095);
    assert(pwm_duty(100, 100) == 4095 && pwm_duty(0, 100) == 0 && pwm_duty(100, 0) == 0);
    assert(pwm_duty(50, 100) == 2048 && pwm_duty(100, 50) == 2048);
    s.enabled = true;
    s.brightness = 999;
    s.colour = {255, 255, 255};
    s.mode = static_cast<Mode>(255);
    s.speed = 999;
    sanitize(s);
    assert(!s.enabled && s.brightness == 100 && s.speed == 1 && s.mode == Mode::solid);
    assert((s.colour == Colour{100, 100, 100}));
    Touch touch;
    for (int i = 0; i < 20; ++i)
        assert(touch.sample(true, Action::power) == Action::none);
    for (int i = 0; i < 3; ++i)
        assert(touch.sample(false, Action::none) == Action::none);
    for (int i = 0; i < 100; ++i)
        assert(touch.sample(true, Action::power) == Action::none);
    assert(touch.sample(false, Action::none) == Action::none);
    assert(touch.sample(true, Action::power) == Action::none); // transient missed touch
    assert(touch.sample(false, Action::none) == Action::none);
    assert(touch.sample(false, Action::none) == Action::none);
    assert(touch.sample(false, Action::none) == Action::power);
    assert(touch.sample(false, Action::none) == Action::none);
    touch.sample(true, Action::power);
    touch.sample(true, Action::console); // dragging cancels
    for (int i = 0; i < 3; ++i)
        assert(touch.sample(false, Action::none) == Action::none);
    for (bool light : {false, true})
        for (Page page : {Page::colours, Page::patterns, Page::mixer}) {
            View v;
            v.state.light = light;
            v.state.page = page;
            v.state.enabled = true;
            v.actual = {0, 0, 100};
            Button b[18];
            auto n = buttons(v, b, 18);
            assert(n <= 18);
            for (unsigned i = 0; i < n; ++i) {
                const auto r = b[i].rect;
                assert(r.h >= 34 && r.w >= 34 && r.x >= 0 && r.y >= 0 && r.x + r.w <= 320 &&
                       r.y + r.h <= 240);
                assert(hit(v, r.x + r.w / 2, r.y + r.h / 2) == b[i].action);
                assert(text_width(b[i].label, 0) <= r.w - 6);
                for (unsigned j = i + 1; j < n; ++j) {
                    const auto q = b[j].rect;
                    assert(r.x + r.w <= q.x || q.x + q.w <= r.x || r.y + r.h <= q.y ||
                           q.y + q.h <= r.y);
                }
            }
            assert(hit(v, -1, 1) == Action::none && hit(v, 320, 100) == Action::none);
            raster(v, std::string(argv[1]) + "/" + (light ? "light-" : "dark-") +
                          (page == Page::colours    ? "colours"
                           : page == Page::patterns ? "patterns"
                                                    : "mixer") +
                          ".ppm");
            v.led_ok = false;
            assert(hit(v, 10, 45) == Action::none);
            assert(hit(v, 250, 220) == Action::console);
            v.console_ok = false;
            assert(hit(v, 250, 220) == Action::none);
            v.settings_ok = false;
            prepare(v);
            assert(scene_valid());
        }
    std::puts("PASS LED patterns/phase/speed, PWM bounds, startup-off, flashlight, "
              "RGB/brightness, touch release/drag and all UI bounds/themes");
}
