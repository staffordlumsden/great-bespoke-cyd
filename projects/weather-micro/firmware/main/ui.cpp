#include "ui.h"
#include "pixel_fonts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace wm {
namespace {
const cyd_fonts::Font &font(unsigned n) {
    return n == 2   ? cyd_fonts::pixelfont_13x7_semibold
           : n == 1 ? cyd_fonts::pixelfont_11x5_thin
                    : cyd_fonts::pixelfont_7x5_thin;
}
constexpr uint16_t rgb(unsigned c) {
    return ((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31);
}
enum class Shape { box, text, line, ellipse, moon };
struct Command {
    Rect r{};
    Shape kind = Shape::box;
    uint16_t colour = 0, other = 0;
    unsigned face = 0, scale = 1;
    bool ring = false, south = false;
    float phase = 0;
    int x2 = 0, y2 = 0;
    char text[57]{};
};
Command commands[224]{};
// Main UI task only: rendering and hit testing reuse this bounded scratch layout.
Button layout[40]{};
unsigned count = 0;
bool valid = true;
const char *keys(unsigned page) {
    return page ? "YZ0123456789 -.,'&()/" : "ABCDEFGHIJKLMNOPQRSTUVWX";
}
struct Scene {
    const View &v;
    uint16_t bg, surface, fg, muted, blue, green, amber, edge, red;
    explicit Scene(const View &view) : v(view) {
        bool l = v.settings.light;
        bg = rgb(l ? 0xa9dcc2 : 0x081115);
        surface = rgb(l ? 0xd6efe7 : 0x172b32);
        fg = rgb(l ? 0x0c3d48 : 0xe4eef0);
        muted = rgb(l ? 0x376e73 : 0x91a9b0);
        blue = rgb(l ? 0x147ba4 : 0x42a5f5);
        green = rgb(l ? 0x21795c : 0x66cf8e);
        amber = rgb(l ? 0xc66b16 : 0xffae42);
        edge = rgb(l ? 0x83b6b6 : 0x29414a);
        red = rgb(l ? 0xb93f50 : 0xef6b72);
    }
    Command *add(Rect r, Shape kind, uint16_t c) {
        if (count == 224) {
            valid = false;
            return nullptr;
        }
        if (r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 || r.x + r.w > 320 || r.y + r.h > 240)
            valid = false;
        auto *p = &commands[count++];
        *p = {};
        p->r = r;
        p->kind = kind;
        p->colour = c;
        return p;
    }
    void box(Rect r, uint16_t c) { add(r, Shape::box, c); }
    void text(const char *s, int x, int y, uint16_t c, unsigned face = 1, unsigned scale = 1,
              int right = 312) {
        char buf[57];
        std::snprintf(buf, sizeof(buf), "%s", s);
        size_t n = std::strlen(buf);
        while (n && x + text_width(buf, face, scale) > right)
            buf[--n] = 0;
        auto *p = add({x, y, std::max(0, right - x), (font(face).ascent + 3) * int(scale)},
                      Shape::text, c);
        if (p) {
            p->face = face;
            p->scale = scale;
            std::strcpy(p->text, buf);
        }
    }
    void centre(const char *s, int left, int right, int y, uint16_t c, unsigned face = 1,
                unsigned scale = 1) {
        char buf[57];
        std::snprintf(buf, sizeof(buf), "%s", s);
        size_t n = std::strlen(buf);
        while (n && text_width(buf, face, scale) > right - left)
            buf[--n] = 0;
        text(buf, left + (right - left - text_width(buf, face, scale)) / 2, y, c, face, scale,
             right);
    }
    void line(int x, int y, int x2, int y2, uint16_t c) {
        auto *p =
            add({std::min(x, x2), std::min(y, y2), std::abs(x2 - x) + 1, std::abs(y2 - y) + 1},
                Shape::line, c);
        if (p) {
            p->x2 = x2;
            p->y2 = y2;
            p->face = unsigned(x);
            p->scale = unsigned(y);
        }
    }
    void ellipse(int x, int y, int rx, int ry, uint16_t c, bool ring = false) {
        auto *p = add({x - rx, y - ry, rx * 2 + 1, ry * 2 + 1}, Shape::ellipse, c);
        if (p)
            p->ring = ring;
    }
    void moon(int x, int y, int radius, float phase, bool south, uint16_t c) {
        auto *p = add({x - radius, y - radius, radius * 2 + 1, radius * 2 + 1}, Shape::moon, c);
        if (p) {
            p->other = edge;
            p->phase = phase;
            p->south = south;
        }
    }
    void icon(int code, bool day, int x, int y) {
        if (code < 0) {
            centre("?", x - 20, x + 20, y + 8, muted, 2);
            return;
        }
        const bool clear = code <= 1, cloud = code >= 2, fog = code == 45 || code == 48,
                   snow = (code >= 71 && code <= 77) || code == 85 || code == 86,
                   storm = code >= 95;
        if (clear || code == 2) {
            if (day) {
                int sx = cloud ? x - 8 : x, sy = cloud ? y + 8 : y + 15;
                ellipse(sx, sy, cloud ? 6 : 8, cloud ? 6 : 8, amber);
                if (!cloud)
                    for (int i = 0; i < 8; ++i) {
                        float a = i * 0.78539816f;
                        line(sx + int(std::sin(a) * 11), sy + int(std::cos(a) * 11),
                             sx + int(std::sin(a) * 14), sy + int(std::cos(a) * 14), amber);
                    }
            } else
                moon(cloud ? x - 8 : x, cloud ? y + 8 : y + 15, cloud ? 7 : 12, .85f, false, amber);
        }
        if (cloud) {
            ellipse(x - 8, y + 17, 8, 7, muted);
            ellipse(x + 4, y + 13, 10, 10, muted);
            box({x - 14, y + 17, 29, 8}, muted);
        }
        if (fog) {
            line(x - 14, y + 29, x + 14, y + 29, fg);
            line(x - 10, y + 34, x + 10, y + 34, fg);
        } else if (snow) {
            for (int i = -1; i <= 1; ++i) {
                int sx = x + i * 10;
                line(sx, y + 29, sx, y + 35, blue);
                line(sx - 3, y + 32, sx + 3, y + 32, blue);
            }
        } else if (storm) {
            line(x + 3, y + 25, x - 3, y + 31, amber);
            line(x - 3, y + 31, x + 3, y + 31, amber);
            line(x + 3, y + 31, x - 2, y + 37, amber);
        } else if (code >= 51) {
            for (int i = -1; i <= 1; ++i)
                line(x + i * 10, y + 28, x + i * 10 - 3, y + 34, blue);
        }
    }
    void number(float c, bool available, char *out, size_t size) {
        if (available)
            std::snprintf(out, size, "%.0f", std::round(temperature(c, v.settings.fahrenheit)));
        else
            std::snprintf(out, size, "--");
    }
    const Day *day() {
        int i = today(v.forecast, v.now);
        return i >= 0 ? &v.forecast.days[i] : nullptr;
    }
    void current() {
        if (!v.settings.count) {
            centre("Choose your location", 8, 312, 83, fg, 2);
            centre("SETTINGS > SEARCH", 8, 312, 120, blue);
            centre("Open-Meteo / no API key", 8, 312, 158, muted, 0);
            return;
        }
        const auto &l = v.settings.places[v.settings.selected];
        centre(l.name, 8, 312, 61, fg, 2);
        char value[24], label[28];
        number(v.forecast.temperature, v.has && v.forecast.temp_ok, value, sizeof(value));
        std::snprintf(label, sizeof(label), "%s %c", value, v.settings.fahrenheit ? 'F' : 'C');
        centre(label, 8, 312, 84, fg, 2, 3);
        centre(v.has ? condition(v.forecast.code) : "Updating", 8, 312, 139, blue);
        const auto *d = v.has ? day() : nullptr;
        char high[16], low[16], range[48];
        number(d ? d->high : 0, d && d->high_ok, high, sizeof(high));
        number(d ? d->low : 0, d && d->low_ok, low, sizeof(low));
        std::snprintf(range, sizeof(range), "HIGH %s   LOW %s", high, low);
        centre(range, 8, 312, 165, muted);
    }
    void hourly() {
        centre("NEXT FIVE HOURS", 8, 312, 54, muted, 0);
        unsigned start = next_hour(v.forecast, v.now);
        for (unsigned i = 0; i < 5; ++i) {
            int left = 8 + int(i) * 62, right = left + 56;
            char time[16] = "--", value[16] = "--";
            if (v.has && start + i < v.forecast.hour_count) {
                const auto &h = v.forecast.hours[start + i];
                hour_label(h.time, v.forecast.offset, time, sizeof(time));
                number(h.temperature, h.temp_ok, value, sizeof(value));
                icon(h.code, h.day, left + 28, 99);
            } else
                icon(-1, true, left + 28, 99);
            centre(time, left, right, 76, fg);
            centre(value, left, right, 155, fg, 2);
        }
        centre(v.settings.fahrenheit ? "Forecast temperature / F" : "Forecast temperature / C", 8,
               312, 183, muted, 0);
    }
    void details() {
        box({8, 51, 148, 73}, surface);
        box({164, 51, 148, 73}, surface);
        box({8, 128, 148, 73}, surface);
        box({164, 128, 148, 73}, surface);
        text("RAIN / 24H mm", 12, 55, fg, 0, 1, 152);
        unsigned start = next_hour(v.forecast, v.now);
        float peak = 0;
        unsigned usable = 0;
        for (unsigned i = 0; i < 24 && start + i < v.forecast.hour_count; ++i)
            if (v.has && v.forecast.hours[start + i].rain_ok) {
                peak = std::max(peak, v.forecast.hours[start + i].rain);
                ++usable;
            }
        if (usable) {
            char top[12];
            std::snprintf(top, sizeof(top), "%.1f", peak);
            text(top, 12, 70, muted, 0, 1, 42);
            float scale = std::max(peak, .1f);
            line(40, 108, 149, 108, edge);
            for (unsigned i = 0; i < 24 && start + i < v.forecast.hour_count; ++i) {
                auto &h = v.forecast.hours[start + i];
                if (h.rain_ok) {
                    int height = int(std::round(h.rain / scale * 34));
                    if (height)
                        box({41 + int(i) * 4, 108 - height, 3, height}, blue);
                } else {
                    line(41 + int(i) * 4, 105, 43 + int(i) * 4, 107, muted);
                    line(41 + int(i) * 4, 107, 43 + int(i) * 4, 105, muted);
                }
            }
            text("0", 12, 101, muted, 0, 1, 38);
            text("Now", 40, 113, muted, 0, 1, 65);
            text("12h", 85, 113, muted, 0, 1, 111);
            text("24h", 125, 113, muted, 0, 1, 151);
        } else
            centre("No rain data", 8, 156, 84, muted, 0);
        text("WIND", 169, 55, fg, 0, 1, 306);
        ellipse(201, 92, 22, 22, edge, true);
        text("N", 198, 68, fg, 0, 1, 207);
        text("W", 181, 89, muted, 0, 1, 190);
        text("E", 215, 89, muted, 0, 1, 225);
        text("S", 198, 108, muted, 0, 1, 207);
        if (v.has && v.forecast.wind_ok) {
            float angle = v.forecast.wind_direction * 0.0174532925f;
            int dx = int(std::sin(angle) * 15), dy = -int(std::cos(angle) * 15);
            if (v.forecast.wind_speed > .1f) {
                line(201 - dx / 2, 92 - dy / 2, 201 + dx, 92 + dy, blue);
                float a = angle + 2.5f;
                line(201 + dx, 92 + dy, 201 + dx + int(std::sin(a) * 7),
                     92 + dy - int(std::cos(a) * 7), blue);
                a = angle - 2.5f;
                line(201 + dx, 92 + dy, 201 + dx + int(std::sin(a) * 7),
                     92 + dy - int(std::cos(a) * 7), blue);
            }
            char speed[12];
            std::snprintf(speed, sizeof(speed), "%.0f", v.forecast.wind_speed);
            centre(speed, 235, 310, 77, fg, 2);
            centre("km/h", 235, 310, 96, muted, 0);
            char from[20];
            std::snprintf(from, sizeof(from), "%s%s", v.forecast.wind_speed > .1f ? "FROM " : "",
                          v.forecast.wind_speed > .1f ? wind_name(v.forecast.wind_direction)
                                                      : "CALM");
            centre(from, 235, 310, 112, muted, 0);
        } else
            centre("--", 235, 310, 88, muted);
        text("SUN / LOCAL", 12, 132, fg, 0, 1, 152);
        const auto *d = v.has ? day() : nullptr;
        line(16, 177, 148, 177, edge);
        for (unsigned i = 1; i <= 24; ++i) {
            float a = float(i - 1) / 24, b = float(i) / 24;
            line(18 + int(a * 128), 177 - int(std::sin(a * 3.14159265f) * 28), 18 + int(b * 128),
                 177 - int(std::sin(b * 3.14159265f) * 28), muted);
        }
        char rise[16] = "--", set[16] = "--";
        if (d) {
            hour_label(d->rise, v.forecast.offset, rise, sizeof(rise), true);
            hour_label(d->set, v.forecast.offset, set, sizeof(set), true);
            float p = sun_progress(*d, v.now);
            if (p >= 0)
                ellipse(18 + int(p * 128), 177 - int(std::sin(p * 3.14159265f) * 28), 4, 4, amber);
            else
                centre(!d->rise || !d->set ? (v.forecast.day ? "Polar day" : "No sun times")
                                           : "Night",
                       21, 143, 159, blue, 0);
        }
        text(rise, 12, 187, muted, 0, 1, 80);
        int width = text_width(set, 0);
        text(set, 151 - width, 187, muted, 0, 1, 152);
        text("MOON", 170, 132, fg, 0, 1, 308);
        if (d && d->moon_ok) {
            moon(190, 171, 19, d->moon,
                 v.settings.count && v.settings.places[v.settings.selected].latitude < 0, fg);
            char name[32];
            std::snprintf(name, sizeof(name), "%s", moon_name(d->moon));
            auto *space = std::strchr(name, ' ');
            if (space) {
                *space = 0;
                text(space + 1, 216, 164, fg, 0, 1, 308);
            }
            text(name, 216, 150, fg, 0, 1, 308);
            char lit[24];
            std::snprintf(lit, sizeof(lit), "~%.0f%% lit",
                          (1 - std::cos(d->moon * 6.28318530718f)) * 50);
            text(lit, 216, 184, muted, 0, 1, 308);
        } else {
            ellipse(190, 171, 19, 19, edge);
            text("No phase", 216, 161, muted, 0, 1, 308);
        }
    }
    void render() {
        box({0, 0, 320, 240}, bg);
        box({0, 0, 320, 34}, surface);
        text("WEATHER", 8, 9, fg, 2, 1, 158);
        text("0.1.1", 173, 16, muted, 0, 1, 237);
        if (v.page != Page::search) {
            const char *status = v.status[0]    ? v.status
                                 : !v.connected ? "Wi-Fi disconnected"
                                 : v.busy       ? "Updating"
                                 : v.stale      ? "STALE / refresh pending"
                                                : "Open-Meteo";
            text(status, 8, 38, v.stale ? amber : muted, 0);
        }
        if (v.page == Page::current)
            current();
        else if (v.page == Page::hourly)
            hourly();
        else if (v.page == Page::details)
            details();
        else if (v.page == Page::search) {
            char buf[47];
            std::snprintf(buf, sizeof(buf), "%s",
                          v.query[0] ? v.query : "City, country / MORE for spaces");
            while (text_width(buf, 1) > 300)
                std::memmove(buf, buf + 1, std::strlen(buf));
            text(buf, 9, 43, fg);
        } else if (v.page == Page::place && v.manage < v.settings.count) {
            auto &l = v.settings.places[v.manage];
            centre(l.name, 8, 312, 66, fg, 2);
            centre(l.admin, 8, 312, 96, muted);
            centre(l.country, 8, 312, 120, blue);
        } else if (v.page == Page::about) {
            const char *lines[] = {
                "Weather: Open-Meteo.com",         "CC BY 4.0 / attribution",
                "Places: GeoNames via Open-Meteo", "Current: weather model data",
                "Rain: mm / wind: km/h",           "Moon: daily phase, approx. lighting",
                "Sun arc: schematic daylight path"};
            for (unsigned i = 0; i < 7; ++i)
                text(lines[i], 12, 61 + int(i) * 18, i < 3 ? blue : muted, 0);
        } else if (v.page == Page::network) {
            text(v.connected ? "WIFI CONNECTED" : "WIFI DISCONNECTED", 12, 65,
                 v.connected ? green : amber, 2);
            text("Use saved network profiles", 12, 99, fg);
            text("New Wi-Fi: File Server > Networks", 12, 124, muted, 0);
            text("Then relaunch Weather Micro", 12, 141, muted, 0);
        } else if (v.page == Page::locations && !v.settings.count)
            centre("No saved locations / ADD", 8, 312, 99, muted);
        else if (v.page == Page::results && !v.results.count)
            centre("No matches / try city, country", 8, 312, 99, muted, 0);
        auto *b = layout;
        unsigned n = buttons(v, b, 40);
        for (unsigned i = 0; i < n; ++i) {
            auto &t = b[i];
            uint16_t accent = !t.enabled ? muted : t.selected ? green : blue;
            box(t.rect, t.selected ? accent : edge);
            box({t.rect.x + 1, t.rect.y + 1, t.rect.w - 2, t.rect.h - 2}, surface);
            if (t.subtitle[0]) {
                text(t.label, t.rect.x + 8, t.rect.y + 4, accent, 1, 1, t.rect.x + t.rect.w - 4);
                text(t.subtitle, t.rect.x + 8, t.rect.y + 22, muted, 0, 1, t.rect.x + t.rect.w - 4);
            } else
                centre(t.label, t.rect.x + 3, t.rect.x + t.rect.w - 3,
                       t.rect.y + (t.rect.h - font(1).ascent) / 2, accent, 1);
        }
    }
};
} // namespace
int text_width(const char *text, unsigned face, unsigned scale) {
    const auto &f = font(face);
    int n = 0;
    for (auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        n += f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first].advance + 1;
    return (n ? n - 1 : 0) * int(scale);
}
unsigned buttons(const View &v, Button *out, unsigned cap) {
    unsigned n = 0;
    auto add = [&](Rect r, Kind k, unsigned value, const char *label, bool enabled = true,
                   bool selected = false, const char *sub = "") {
        if (n >= cap || !out)
            return;
        auto &a = out[n++];
        a = Button{};
        a.rect = r;
        a.action = {k, value};
        a.enabled = enabled;
        a.selected = selected;
        std::snprintf(a.label, sizeof(a.label), "%s", label);
        std::snprintf(a.subtitle, sizeof(a.subtitle), "%s", sub);
    };
    add({244, 0, 76, 34}, Kind::console, 0, "CONSOLE", v.console_ok);
    if (unsigned(v.page) <= unsigned(Page::settings)) {
        const char *tabs[] = {"NOW", "5H", "DETAILS", "SETTINGS"};
        for (unsigned i = 0; i < 4; ++i)
            add({8 + int(i) * 78, 204, 70, 34}, Kind::page, i, tabs[i], true,
                unsigned(v.page) == i);
    } else if (v.page != Page::search)
        add({8, 204, 304, 34}, Kind::back, 0, "BACK");
    if (v.page == Page::settings) {
        add({8, 58, 148, 36}, Kind::unit, 0, v.settings.fahrenheit ? "TEMP: F" : "TEMP: C");
        add({164, 58, 148, 36}, Kind::theme, 0, v.settings.light ? "DARK" : "LIGHT");
        char label[32];
        std::snprintf(label, sizeof(label), "PLACES (%u)", v.settings.count);
        add({8, 100, 148, 36}, Kind::places, 0, label);
        add({164, 100, 148, 36}, Kind::add, 0, "SEARCH + SAVE");
        add({8, 142, 148, 36}, Kind::page, unsigned(Page::network), "NETWORK");
        add({164, 142, 148, 36}, Kind::page, unsigned(Page::about), "ABOUT");
    }
    if (v.page == Page::locations || v.page == Page::results) {
        bool saved = v.page == Page::locations;
        unsigned total = saved ? v.settings.count : v.results.count,
                 page = saved ? v.list_page : v.result_page;
        for (unsigned row = 0; row < 3 && page * 3 + row < total; ++row) {
            unsigned i = page * 3 + row;
            const auto &l = saved ? v.settings.places[i] : v.results.places[i];
            char sub[38];
            std::snprintf(sub, sizeof(sub), "%.28s / %s", l.admin, l.country);
            add({8, 52 + int(row) * 40, 304, 36}, saved ? Kind::place : Kind::result, i, l.name,
                true, saved && i == v.settings.selected, sub);
        }
        add({8, 168, 96, 34}, Kind::previous, 0, "PREV", page > 0);
        if (saved)
            add({112, 168, 96, 34}, Kind::add, 0, "ADD");
        add({216, 168, 96, 34}, Kind::next, 0, "NEXT", (page + 1) * 3 < total);
    }
    if (v.page == Page::place) {
        add({8, 153, 148, 38}, Kind::use, 0, "USE LOCATION");
        add({164, 153, 148, 38}, Kind::remove, 0, "REMOVE");
    }
    if (v.page == Page::network) {
        add({8, 159, 148, 36}, Kind::retry, 0, "RETRY", !v.busy);
        add({164, 159, 148, 36}, Kind::wifi_next, 0, "NEXT NETWORK", !v.busy);
    }
    if (v.page == Page::search) {
        const char *chars = keys(v.keyboard_page);
        for (unsigned i = 0; chars[i]; ++i) {
            char label[4] = {chars[i], 0};
            if (chars[i] == ' ')
                std::strcpy(label, "SP");
            add({8 + int(i % 8) * 38, 71 + int(i / 8) * 38, 34, 34}, Kind::key, unsigned(chars[i]),
                label);
        }
        add({8, 198, 60, 40}, Kind::back, 0, "BACK");
        add({72, 198, 68, 40}, Kind::keyboard, 0, v.keyboard_page ? "ABC" : "MORE");
        add({144, 198, 64, 40}, Kind::erase, 0, "DEL");
        add({212, 198, 100, 40}, Kind::search, 0, "SEARCH",
            v.connected && v.clock_valid && !v.busy && std::strlen(v.query) >= 2);
    }
    return n;
}
Action hit(const View &v, int x, int y) {
    auto *b = layout;
    unsigned n = buttons(v, b, 40);
    for (unsigned i = 0; i < n; ++i)
        if (b[i].rect.contains(x, y))
            return b[i].enabled ? b[i].action : Action{};
    return {};
}
void prepare(const View &v) {
    count = 0;
    valid = true;
    Scene(v).render();
}
bool scene_valid() { return valid; }
uint16_t pixel(int x, int y, void *) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240)
        return 0;
    uint16_t colour = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto &c = commands[i];
        if (!c.r.contains(x, y))
            continue;
        if (c.kind == Shape::box) {
            colour = c.colour;
            continue;
        }
        if (c.kind == Shape::line) {
            int dx = c.x2 - int(c.face), dy = c.y2 - int(c.scale);
            if (std::abs((x - int(c.face)) * dy - (y - int(c.scale)) * dx) <=
                std::max(std::abs(dx), std::abs(dy)) / 2)
                colour = c.colour;
            continue;
        }
        if (c.kind == Shape::ellipse || c.kind == Shape::moon) {
            float rx = (c.r.w - 1) * .5f, ry = (c.r.h - 1) * .5f, px = (x - c.r.x - rx) / rx,
                  py = (y - c.r.y - ry) / ry, r = px * px + py * py;
            if (r <= 1) {
                if (c.kind == Shape::moon)
                    colour = moon_lit(px, py, c.phase, c.south) ? c.colour : c.other;
                else if (!c.ring || r >= .82f)
                    colour = c.colour;
            }
            continue;
        }
        const auto &f = font(c.face);
        int cursor = c.r.x, scale = int(c.scale);
        for (auto *p = reinterpret_cast<const unsigned char *>(c.text); *p; ++p) {
            auto &g = f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first];
            int gx = (x - cursor) / scale - g.x, gy = (y - c.r.y) / scale - f.ascent - g.y;
            if (x >= cursor && gx >= 0 && gy >= 0 && gx < g.width && gy < g.height) {
                unsigned bit = gy * g.width + gx;
                if (f.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8)))
                    colour = c.colour;
            }
            cursor += (g.advance + 1) * scale;
            if (cursor > x)
                break;
        }
    }
    return colour;
}
} // namespace wm
