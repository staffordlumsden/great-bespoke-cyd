#pragma once
#include "weather_model.h"
namespace wm {
enum class Page {
    current,
    hourly,
    details,
    settings,
    locations,
    place,
    search,
    results,
    network,
    about
};
enum class Kind {
    none,
    page,
    theme,
    unit,
    console,
    places,
    add,
    place,
    use,
    remove,
    previous,
    next,
    key,
    keyboard,
    erase,
    search,
    result,
    retry,
    wifi_next,
    refresh,
    back
};
struct Action {
    Kind kind = Kind::none;
    unsigned value = 0;
    bool operator==(const Action &a) const { return kind == a.kind && value == a.value; }
    bool operator!=(const Action &a) const { return !(*this == a); }
};
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct Button {
    Rect rect;
    Action action;
    char label[42]{}, subtitle[38]{};
    bool selected = false, enabled = true;
};
struct View {
    Settings settings{};
    Forecast forecast{};
    SearchResults results{};
    Page page = Page::current;
    unsigned list_page = 0, result_page = 0, manage = 0, keyboard_page = 0;
    bool has = false, busy = false, connected = false, clock_valid = false, stale = false,
         console_ok = true;
    char query[41]{}, status[45]{};
    int64_t now = 0;
};
unsigned buttons(const View &, Button *, unsigned);
Action hit(const View &, int, int);
void prepare(const View &);
uint16_t pixel(int, int, void *user = nullptr);
bool scene_valid();
int text_width(const char *, unsigned face = 1, unsigned scale = 1);
} // namespace wm
