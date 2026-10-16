#include "ui.h"
#include "pixel_fonts.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace ou {
namespace {
const cyd_fonts::Font &font(unsigned f) {
    return f == 0   ? cyd_fonts::pixelfont_7x5_thin
           : f == 2 ? cyd_fonts::pixelfont_13x7_semibold
                    : cyd_fonts::pixelfont_11x5_thin;
}
constexpr uint16_t rgb(unsigned c) {
    return ((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31);
}
struct Command {
    Rect rect;
    uint16_t colour;
    bool label = false;
    unsigned face = 0;
    char text[65]{};
};
Command commands[100]{};
unsigned command_count = 0;
struct Scene {
    const View &v;
    uint16_t bg, surface, fg, muted, blue, green, amber, red, edge;
    explicit Scene(const View &view) : v(view) {
        bg = rgb(v.light ? 0xa9dcc2 : 0x081115);
        surface = rgb(v.light ? 0xd6efe7 : 0x172b32);
        fg = rgb(v.light ? 0x0c3d48 : 0xe4eef0);
        muted = rgb(v.light ? 0x376e73 : 0x91a9b0);
        blue = rgb(v.light ? 0x147ba4 : 0x42a5f5);
        green = rgb(v.light ? 0x21795c : 0x66cf8e);
        amber = rgb(v.light ? 0xc66b16 : 0xffae42);
        red = rgb(v.light ? 0xb93f50 : 0xef6b72);
        edge = rgb(v.light ? 0x83b6b6 : 0x29414a);
        box(0, 0, 320, 240, bg);
    }
    void box(int left, int top, int w, int h, uint16_t c) {
        if (command_count < 100)
            commands[command_count++] = {{left, top, w, h}, c, false, 0, {}};
    }
    void text(const char *s, int left, int top, uint16_t c, unsigned face = 1, int right = 312) {
        if (command_count >= 100)
            return;
        auto &cmd = commands[command_count++];
        cmd = {{left, top, std::max(0, right - left), font(face).ascent + 3}, c, true, face, {}};
        std::snprintf(cmd.text, sizeof(cmd.text), "%s", s);
    }
    void button(const char *label, Rect r, uint16_t accent) {
        box(r.x, r.y, r.w, r.h, edge);
        box(r.x + 1, r.y + 1, r.w - 2, r.h - 2, surface);
        text(label, r.x + (r.w - text_width(label, 0)) / 2, r.y + (r.h - 7) / 2, accent, 0,
             r.x + r.w - 2);
    }
    uint16_t level(double n) { return n >= 90 ? red : n >= 75 ? amber : green; }
    void meter(const char *title, double used, int top, int64_t reset, uint16_t accent) {
        box(8, top, 304, 60, surface);
        text(title, 16, top + 6, fg);
        char number[28];
        std::snprintf(number, sizeof(number), "%.1f%% USED", used);
        text(number, 198, top + 8, level(used), 0);
        box(16, top + 27, 288, 7, edge);
        box(16, top + 27, int(288 * std::clamp(used, 0.0, 100.0) / 100), 7, accent);
        char reset_text[40];
        countdown(reset, v.epoch, reset_text, sizeof(reset_text));
        text(reset_text, 16, top + 43, muted, 0);
    }

    void metric_stat(unsigned index, int y) {
        if (index >= v.metrics.stat_count)
            return;
        text(v.metrics.stats[index].label, 16, y, muted, 0, 178);
        text(v.metrics.stats[index].value, 183, y, fg, 0, 304);
    }
    void generic() {
        if (!v.has_balance) {
            box(8, 61, 304, 139, surface);
            text(v.key ? "Waiting for provider" : "Configure this source", 16, 78, fg, 2);
            text(v.key ? "Wi-Fi and valid time required" : "/private/usage.json", 16, 110, blue);
            text("Codex: Mac bridge must be awake", 16, 145, muted, 0);
            text("Tap source name to switch", 16, 174, muted, 0);
            return;
        }
        auto &m = v.metrics;
        if (m.meter_count == 2) {
            meter(m.meters[0].label, m.meters[0].used, 61, m.meters[0].reset, amber);
            meter(m.meters[1].label, m.meters[1].used, 125, m.meters[1].reset, blue);
        } else if (m.meter_count == 1) {
            meter(m.meters[0].label, m.meters[0].used, 61, m.meters[0].reset, blue);
            box(8, 125, 304, 60, surface);
            for (unsigned i = 0; i < 3; ++i)
                metric_stat(i, 130 + 18 * i);
        } else {
            box(8, 61, 304, 124, surface);
            text("KEY SPENDING", 16, 72, fg);
            for (unsigned i = 0; i < 3; ++i)
                metric_stat(i, 102 + 26 * i);
        }
        if (v.stale_balance)
            text("STALE / waiting for a fresh update", 10, 190, amber, 0);
        else if (m.meter_count == 2 && m.stat_count) {
            char line[48];
            std::snprintf(line, sizeof(line), "%s %s", m.stats[0].label, m.stats[0].value);
            text(line, 10, 190, muted, 0);
        } else
            text(v.kind == ProviderKind::openrouter ? "API key budget"
                                                    : "Via Mac / keep the host awake",
                 10, 190, muted, 0);
    }
    void sources() {
        for (unsigned i = 0; i < v.provider_count; ++i) {
            char label[40];
            std::snprintf(label, sizeof(label), "%s%s", v.provider_names[i],
                          v.provider_ready[i] ? "" : " / SET UP");
            button(label, {8, 43 + int(i) * 39, 304, 35}, i == v.selected_provider ? green : blue);
        }
        if (v.provider_count < max_providers)
            button("+ ADD A PROVIDER", {8, 43 + int(v.provider_count) * 39, 304, 35}, blue);
    }
    void add() {
        box(8, 43, 304, 159, surface);
        text("ADD A PROVIDER", 16, 53, fg, 2);
        text("Edit private SD configuration:", 16, 82, muted, 0);
        text("/private/usage.json", 16, 102, blue);
        text("Ollama / Codex / OpenRouter", 16, 128, fg, 0);
        text("Or add an authenticated JSON feed", 16, 147, muted, 0);
        text("Up to four sources. See app guide.", 16, 174, muted, 0);
    }
    void balance() {
        if (v.kind != ProviderKind::ollama) {
            generic();
            return;
        }
        if (!v.has_balance) {
            box(8, 64, 304, 128, surface);
            text(v.key ? "Waiting for usage" : "Add your API key", 16, 77, fg, 2);
            text(v.key ? "Wi-Fi and valid time required" : "Private SD file:", 16, 105, muted, 0);
            text(v.key ? "Refresh is once per minute" : "/private/usage.json", 16, 123, blue, 1);
            text("Tap NET for setup / connection", 16, 151, muted, 0);
            text("API key stays on your SD card", 16, 173, muted, 0);
            return;
        }
        const auto &b = v.balance;
        if (b.kind == BalanceKind::legacy) {
            meter("SESSION", b.session_used, 61, b.session_reset, amber);
            meter("WEEKLY", b.weekly_used, 125, b.weekly_reset, blue);
        } else {
            box(8, 61, 304, 124, surface);
            text("INCLUDED CREDITS", 16, 68, fg);
            char remaining[24], allowance[24], line[64];
            amount(b.remaining, remaining, sizeof(remaining));
            amount(b.allowance, allowance, sizeof(allowance));
            std::snprintf(line, sizeof(line), "%s left / %s", remaining, allowance);
            text(line, 16, 91, blue, 2);
            box(16, 119, 288, 8, edge);
            if (b.percent_known)
                box(16, 119, int(288 * std::clamp(b.used_percent, 0.0, 100.0) / 100), 8,
                    level(b.used_percent));
            if (b.percent_known)
                std::snprintf(line, sizeof(line), "%.1f%% USED", b.used_percent);
            else
                std::snprintf(line, sizeof(line), "No included allowance");
            text(line, 16, 135, muted, 0);
            countdown(b.period_until, v.epoch, line, sizeof(line));
            text(line, 16, 158, muted, 0);
        }
        char money[24], line[50];
        amount(b.purchased, money, sizeof(money));
        std::snprintf(line, sizeof(line), "Purchased %s%s", money,
                      v.stale_balance ? "  STALE" : "");
        text(line, 10, 190, v.stale_balance ? amber : muted, 0);
    }
    void activity() {
        if (v.kind != ProviderKind::ollama) {
            box(8, 61, 304, 141, surface);
            text("PROVIDER DETAILS", 16, 72, fg);
            for (unsigned i = 0; i < 3; ++i)
                metric_stat(i, 102 + 23 * i);
            text("No hourly history supplied", 16, 185, muted, 0);
            return;
        }
        box(8, 61, 304, 141, surface);
        if (!v.has_usage) {
            text("24H ACTIVITY", 16, 72, fg);
            text("Waiting for API history", 16, 103, muted);
            return;
        }
        char requests[24], cost[24], line[60];
        count(v.usage.requests, requests, sizeof(requests));
        amount(v.usage.cost, cost, sizeof(cost));
        std::snprintf(line, sizeof(line), "%s requests / 24H", requests);
        text(line, 16, 68, fg, 2);
        std::snprintf(line, sizeof(line), "Cost %s%s", v.usage.cost_known ? cost : "not supplied",
                      v.stale_usage ? "  STALE" : "");
        text(line, 16, 92, v.stale_usage ? amber : muted, 0);
        char input[16], output[16];
        count(v.usage.input, input, sizeof(input));
        count(v.usage.output, output, sizeof(output));
        std::snprintf(line, sizeof(line), "Tokens IN %s / OUT %s",
                      v.usage.input_known ? input : "--", v.usage.output_known ? output : "--");
        text(line, 16, 108, muted, 0);
        uint64_t maximum = 1;
        for (unsigned i = 0; i < v.usage.bucket_count; ++i)
            maximum = std::max(maximum, v.usage.buckets[i]);
        box(16, 179, 288, 1, edge);
        const unsigned n = v.usage.bucket_count;
        for (unsigned i = 0; i < n; ++i) {
            int left = 16 + int(i * 288 / n);
            int end = 16 + int((i + 1) * 288 / n);
            int h = int(47.0 * v.usage.buckets[i] / maximum);
            box(left, 179 - h, end - left - 2, h, i + 1 == n ? amber : blue);
        }
        text("Hourly requests / UTC / now >", 16, 188, muted, 0);
    }
    void network() {
        box(8, 61, 304, 141, surface);
        char line[60];
        std::snprintf(line, sizeof(line), "%u saved Wi-Fi profile%s", v.networks,
                      v.networks == 1 ? "" : "s");
        text(line, 16, 68, fg);
        text(v.key ? "Source ready / verified HTTPS" : "Source configuration incomplete", 16, 91,
             v.key ? green : amber, 0);
        text("/private/usage.json", 16, 109, blue);
        text("New LAN: File Server > Network", 16, 134, muted, 0);
        text("Edit sources via SD card reader", 16, 149, muted, 0);
        button("RETRY", {12, 164, 92, 34}, v.busy ? muted : blue);
        button("NEXT", {114, 164, 92, 34}, v.busy ? muted : blue);
        button("RELOAD SD", {216, 164, 92, 34}, v.busy ? muted : blue);
    }
    void render() {
        box(0, 0, 320, 34, surface);
        text("USAGE", 8, 7, fg, 2, 75);
        text("0.2.0", 73, 13, muted, 0, 110);
        button(v.provider_name, {117, 0, 151, 34}, blue);
        button("NET", {279, 0, 41, 34}, blue);
        if (v.page != Page::sources && v.page != Page::add) {
            box(8, 43, 5, 5, v.connected ? green : red);
            text(v.status, 20, 42, v.busy ? amber : muted, 0);
        }
        if (v.page == Page::sources)
            sources();
        else if (v.page == Page::add)
            add();
        else if (v.page == Page::balance)
            balance();
        else if (v.page == Page::activity)
            activity();
        else
            network();
        button(v.busy ? "FETCHING" : "REFRESH", {8, 206, 74, 34}, v.busy ? muted : blue);
        button(v.page == Page::balance ? (v.kind == ProviderKind::ollama ? "24H" : "DETAILS")
                                       : "USAGE",
               {86, 206, 74, 34}, blue);
        button(v.light ? "DARK" : "LIGHT", {164, 206, 70, 34}, blue);
        button("CONSOLE", {238, 206, 74, 34}, blue);
    }
};
} // namespace
int text_width(const char *text, unsigned face) {
    const auto &f = font(face);
    int n = 0;
    for (auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        n += f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first].advance + 1;
    return n ? n - 1 : 0;
}

unsigned buttons(const View &v, Button *out, unsigned cap) {
    Button b[12] = {{{8, 206, 74, 34}, Action::refresh}, {{86, 206, 74, 34}, Action::page},
                    {{164, 206, 70, 34}, Action::theme}, {{238, 206, 74, 34}, Action::console},
                    {{279, 0, 41, 34}, Action::network}, {{117, 0, 151, 34}, Action::sources}};
    unsigned n = 6;
    if (v.page == Page::network) {
        b[n++] = {{12, 164, 92, 34}, Action::retry};
        b[n++] = {{114, 164, 92, 34}, Action::next};
        b[n++] = {{216, 164, 92, 34}, Action::reload};
    }
    if (v.page == Page::sources) {
        for (unsigned i = 0; i < v.provider_count && i < max_providers; ++i)
            b[n++] = {{8, 43 + int(i) * 39, 304, 35},
                      static_cast<Action>(int(Action::source0) + i)};
        if (v.provider_count < max_providers)
            b[n++] = {{8, 43 + int(v.provider_count) * 39, 304, 35}, Action::add};
    }
    n = std::min(n, cap);
    std::copy(b, b + n, out);
    return n;
}
Action hit(const View &v, int x, int y) {
    Button b[12];
    auto n = buttons(v, b, 12);
    for (unsigned i = 0; i < n; ++i)
        if (b[i].rect.contains(x, y))
            return v.busy && (b[i].action == Action::refresh || b[i].action == Action::retry ||
                              b[i].action == Action::next || b[i].action == Action::reload)
                       ? Action::none
                       : b[i].action;
    return Action::none;
}
void prepare(const View &view) {
    command_count = 0;
    Scene s(view);
    s.render();
}
uint16_t pixel(int x, int y, void *) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240)
        return 0;
    uint16_t colour = 0;
    for (unsigned i = 0; i < command_count; ++i) {
        const auto &cmd = commands[i];
        if (!cmd.rect.contains(x, y))
            continue;
        if (!cmd.label) {
            colour = cmd.colour;
            continue;
        }
        const auto &f = font(cmd.face);
        int cursor = cmd.rect.x;
        for (const auto *p = reinterpret_cast<const unsigned char *>(cmd.text); *p; ++p) {
            const auto &g = f.glyphs[(*p >= f.first && *p <= f.last ? *p : '?') - f.first];
            if (cursor + g.advance > cmd.rect.x + cmd.rect.w)
                break;
            int gx = x - cursor - g.x, gy = y - cmd.rect.y - f.ascent - g.y;
            if (gx >= 0 && gy >= 0 && gx < g.width && gy < g.height) {
                unsigned bit = gy * g.width + gx;
                if (f.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8)))
                    colour = cmd.colour;
            }
            cursor += g.advance + 1;
            if (cursor > x)
                break;
        }
    }
    return colour;
}
} // namespace ou
