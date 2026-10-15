#pragma once
#include "usage_model.h"
#include <cstdint>
namespace ou {
enum class Page { balance, activity, network };
enum class Action { none, refresh, page, theme, console, network, retry, next, reload };
struct View {
    bool light = false, connected = false, busy = false, sleeping = false, key = false,
         has_balance = false, has_usage = false;
    bool stale_balance = false, stale_usage = false;
    Page page = Page::balance;
    Balance balance{};
    Usage usage{};
    int64_t epoch = 0;
    uint64_t now = 0, updated = 0, usage_updated = 0, next_poll = 0;
    char status[45] = "Starting", time[12] = "--:--";
    unsigned networks = 0;
};
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct Button {
    Rect rect;
    Action action;
};
unsigned buttons(const View &, Button *, unsigned);
Action hit(const View &, int, int);
void prepare(const View &);
uint16_t pixel(int, int, void *);
int text_width(const char *, unsigned font = 1);
} // namespace ou
