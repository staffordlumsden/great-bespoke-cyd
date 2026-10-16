#pragma once
#include "led_model.h"
#include <cstdint>
namespace ledapp {
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct Button {
    Rect rect;
    Action action;
    const char *label;
    unsigned accent;
    bool selected;
};
struct View {
    State state{};
    Colour actual{};
    bool led_ok = true;
    bool console_ok = true;
    bool settings_ok = true;
};
unsigned buttons(const View &view, Button *out, unsigned capacity);
Action hit(const View &view, int x, int y);
int text_width(const char *text, unsigned face);
void prepare(const View &view);
uint16_t pixel(int x, int y, void *user = nullptr);
bool scene_valid();
} // namespace ledapp
