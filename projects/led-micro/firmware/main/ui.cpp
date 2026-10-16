#include "ui.h"
#include "pixel_fonts.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ledapp {
namespace {
constexpr uint16_t rgb(unsigned c) {
    return ((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31);
}
const cyd_fonts::Font &font(unsigned f) {
    return f == 2   ? cyd_fonts::pixelfont_13x7_semibold
           : f == 1 ? cyd_fonts::pixelfont_11x5_thin
                    : cyd_fonts::pixelfont_7x5_thin;
}
struct Command {
    Rect r;
    uint16_t colour;
    bool label;
    unsigned face;
    char text[48];
};
Command commands[96]{};
unsigned count = 0;
bool valid = true;
struct Scene {
    const View &v;
    uint16_t bg, surface, fg, muted, blue, green, red, edge;
    explicit Scene(const View &view) : v(view) {
        const bool l = v.state.light;
        bg = rgb(l ? 0xa9dcc2 : 0x081115);
        surface = rgb(l ? 0xd6efe7 : 0x172b32);
        fg = rgb(l ? 0x0c3d48 : 0xe4eef0);
        muted = rgb(l ? 0x376e73 : 0x91a9b0);
        blue = rgb(l ? 0x147ba4 : 0x42a5f5);
        green = rgb(l ? 0x21795c : 0x66cf8e);
        red = rgb(l ? 0xb93f50 : 0xef6b72);
        edge = rgb(l ? 0x83b6b6 : 0x29414a);
    }
    void box(Rect r, uint16_t colour) {
        if (count == 96) {
            valid = false;
            return;
        }
        if (r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 || r.x + r.w > 320 || r.y + r.h > 240)
            valid = false;
        commands[count++] = {r, colour, false, 0, {}};
    }
    void text(const char *s, int x, int y, uint16_t colour, unsigned face = 0, int right = 312) {
        if (count == 96) {
            valid = false;
            return;
        }
        if (x + text_width(s, face) > right)
            valid = false;
        auto &c = commands[count++];
        c = {{x, y, right - x, font(face).ascent + 3}, colour, true, face, {}};
        std::snprintf(c.text, sizeof(c.text), "%s", s);
        if (c.r.x < 0 || c.r.y < 0 || c.r.x + c.r.w > 320 || c.r.y + c.r.h > 240)
            valid = false;
    }
    void render() {
        const auto &s = v.state;
        box({0, 0, 320, 240}, bg);
        box({0, 0, 320, 36}, surface);
        // Small bulb icon in the existing flat geometric icon language.
        box({12, 7, 12, 2}, blue);
        box({8, 11, 2, 10}, blue);
        box({26, 11, 2, 10}, blue);
        box({10, 9, 2, 4}, blue);
        box({24, 9, 2, 4}, blue);
        box({10, 21, 4, 2}, blue);
        box({22, 21, 4, 2}, blue);
        box({14, 23, 8, 3}, blue);
        box({14, 29, 8, 2}, blue);
        text("LED MICRO", 36, 10, fg, 2, 184);
        text("0.1.0", 188, 16, muted, 0, 238);
        Button b[18];
        const auto n = buttons(v, b, 18);
        for (unsigned i = 0; i < n; ++i) {
            const auto &t = b[i];
            const auto r = t.rect;
            uint16_t accent = t.accent == 1   ? green
                              : t.accent == 2 ? red
                              : t.accent == 3 ? rgb(s.light ? 0xc66b16 : 0xffae42)
                              : t.accent == 4 ? fg
                                              : blue;
            if (t.action == Action::console && !v.console_ok)
                accent = muted;
            if (!v.led_ok && t.action != Action::theme && t.action != Action::console)
                accent = muted;
            box(r, t.selected ? accent : edge);
            box({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, surface);
            const unsigned face = text_width(t.label, 1) <= r.w - 8 ? 1 : 0;
            text(t.label, r.x + (r.w - text_width(t.label, face)) / 2,
                 r.y + (r.h - font(face).ascent) / 2, accent, face, r.x + r.w - 3);
        }
        if (s.page != Page::mixer) {
            const unsigned preview = (unsigned(v.actual.r) * 255 / 100 << 16) |
                                     (unsigned(v.actual.g) * 255 / 100 << 8) |
                                     (unsigned(v.actual.b) * 255 / 100);
            box({110, 44, 26, 26}, edge);
            box({112, 46, 22, 22}, rgb(preview));
            text(v.led_ok ? mode_name(s.mode) : "LED ERROR", 146, 45, v.led_ok ? fg : red, 0);
            text(!v.settings_ok ? "SETTINGS UNSAVED"
                 : s.enabled    ? (s.brightness ? "OUTPUT ENABLED" : "BRIGHTNESS ZERO")
                                : "OUTPUT OFF",
                 146, 62, muted, 0);
        } else {
            text("RGB MIX", 218, 46, fg, 1);
            for (unsigned i = 0; i < 3; ++i) {
                const unsigned value = i == 0 ? s.colour.r : i == 1 ? s.colour.g : s.colour.b;
                char line[28];
                std::snprintf(line, sizeof(line), "%s   %u%%",
                              i == 0   ? "RED"
                              : i == 1 ? "GREEN"
                                       : "BLUE",
                              value);
                text(line, 62, 86 + int(i) * 40, fg, 1, 264);
                box({58, 108 + int(i) * 40, 204, 5}, edge);
                box({58, 108 + int(i) * 40, int(204 * value / 100), 5}, blue);
            }
        }
        char value[12];
        std::snprintf(value, sizeof(value), "%u%%", s.brightness);
        text("BRIGHT", 52, 208, muted, 0, 110);
        text(value, 119, 208, fg, 0, 164);
        box({52, 224, 110, 5}, edge);
        box({52, 224, int(110 * s.brightness / 100), 5}, blue);
    }
};
} // namespace
int text_width(const char *s, unsigned face) {
    int w = 0;
    const auto &f = font(face);
    for (auto p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        w += f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first].advance + 1;
    return w ? w - 1 : 0;
}
unsigned buttons(const View &v, Button *out, unsigned capacity) {
    Button b[18]{};
    unsigned n = 0;
    const auto &s = v.state;
    auto add = [&](Rect r, Action a, const char *label, unsigned accent = 0,
                   bool selected = false) { b[n++] = {r, a, label, accent, selected}; };
    add({244, 0, 76, 36}, Action::theme, s.light ? "DARK" : "LIGHT");
    add({8, 202, 36, 36}, Action::dimmer, "-");
    add({168, 202, 36, 36}, Action::brighter, "+");
    add({212, 202, 100, 36}, Action::console, "CONSOLE");
    if (s.page == Page::mixer) {
        add({8, 40, 92, 36}, Action::back, "BACK");
        add({108, 40, 96, 36}, Action::power, s.enabled ? "LED ON" : "LED OFF", s.enabled ? 1 : 2);
        for (unsigned i = 0; i < 3; ++i) {
            add({8, 82 + int(i) * 40, 40, 34}, static_cast<Action>(int(Action::red_down) + i * 2),
                "-");
            add({272, 82 + int(i) * 40, 40, 34}, static_cast<Action>(int(Action::red_up) + i * 2),
                "+");
        }
    } else {
        add({8, 40, 94, 36}, Action::power, s.enabled ? "LED ON" : "LED OFF", s.enabled ? 1 : 2);
        add({8, 82, 96, 34}, Action::colours, "COLOUR", 0, s.page == Page::colours);
        add({112, 82, 96, 34}, Action::patterns, "PATTERN", 0, s.page == Page::patterns);
        add({216, 82, 96, 34}, Action::flashlight, "FLASHLIGHT", 1);
        if (s.page == Page::colours) {
            const Action a[] = {Action::red,    Action::green, Action::blue,
                                Action::yellow, Action::white, Action::mixer};
            const char *labels[] = {"RED", "GREEN", "BLUE", "YELLOW", "WHITE", "RGB MIX"};
            const Colour colours[] = {{100, 0, 0},   {0, 100, 0},     {0, 0, 100},
                                      {100, 100, 0}, {100, 100, 100}, {}};
            for (unsigned i = 0; i < 6; ++i)
                add({8 + int(i % 3) * 104, 122 + int(i / 3) * 40, 96, 34}, a[i], labels[i],
                    i == 0   ? 2
                    : i == 1 ? 1
                    : i == 3 ? 3
                    : i == 4 ? 4
                             : 0,
                    i < 5 && s.colour == colours[i]);
        } else {
            const Action a[] = {Action::solid,        Action::flash,       Action::blue_red,
                                Action::green_yellow, Action::green_white, Action::speed};
            const char *speeds[] = {"SPEED .5X", "SPEED 1X", "SPEED 2X"};
            const char *labels[] = {"SOLID",        "FLASH",       "BLUE/RED",
                                    "GREEN/YELLOW", "GREEN/WHITE", speeds[std::min(s.speed, 2u)]};
            for (unsigned i = 0; i < 6; ++i)
                add({8 + int(i % 3) * 104, 122 + int(i / 3) * 40, 96, 34}, a[i], labels[i], 0,
                    i < 5 && unsigned(s.mode) == i);
        }
    }
    n = std::min(n, capacity);
    std::copy(b, b + n, out);
    return n;
}
Action hit(const View &v, int x, int y) {
    Button b[18];
    const auto n = buttons(v, b, 18);
    for (unsigned i = 0; i < n; ++i)
        if (b[i].rect.contains(x, y)) {
            if (b[i].action == Action::console)
                return v.console_ok ? Action::console : Action::none;
            if (!v.led_ok && b[i].action != Action::theme)
                return Action::none;
            return b[i].action;
        }
    return Action::none;
}
void prepare(const View &view) {
    count = 0;
    valid = true;
    Scene(view).render();
}
bool scene_valid() { return valid; }
uint16_t pixel(int x, int y, void *) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240)
        return 0;
    uint16_t c = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto &cmd = commands[i];
        if (!cmd.r.contains(x, y))
            continue;
        if (!cmd.label) {
            c = cmd.colour;
            continue;
        }
        const auto &f = font(cmd.face);
        int cursor = cmd.r.x;
        for (auto p = reinterpret_cast<const unsigned char *>(cmd.text); *p; ++p) {
            const auto &g = f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first];
            if (cursor + g.advance > cmd.r.x + cmd.r.w)
                break;
            const int gx = x - cursor - g.x, gy = y - cmd.r.y - f.ascent - g.y;
            if (gx >= 0 && gy >= 0 && gx < g.width && gy < g.height) {
                const unsigned bit = gy * g.width + gx;
                if (f.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8)))
                    c = cmd.colour;
            }
            cursor += g.advance + 1;
            if (cursor > x)
                break;
        }
    }
    return c;
}
} // namespace ledapp
