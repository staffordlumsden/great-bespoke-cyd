#pragma once
#include "terminal_parser.h"
#include "esp_err.h"
namespace terminal::ui {struct Scene;}

namespace cm {

struct TerminalGeometry {
    unsigned columns;
    unsigned rows;
    unsigned font_width;
    unsigned font_height;
};

TerminalGeometry terminal_geometry();
bool terminal_zoom(int direction);
unsigned terminal_font_index();
void terminal_set_font_index(unsigned index);
void terminal_set_light_mode(bool light);
// Main task lends its scene; menu and terminal header render serially.
void terminal_use_ui_scene(terminal::ui::Scene* scene);

bool terminal_begin(TerminalParser::Reply reply, void* context);
bool terminal_end();
bool terminal_feed(const uint8_t* data, size_t size);
bool terminal_application_cursor();
void terminal_redraw();
bool terminal_reset();
bool terminal_check_colours();
void terminal_scroll(int rows);
void terminal_live();

esp_err_t terminal_render(unsigned budget, bool wifi_connected, bool keyboard_connected, const char* host_label = "");
void terminal_metrics();

} // namespace cm
