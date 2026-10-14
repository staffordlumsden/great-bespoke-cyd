#include "terminal_renderer.h"
#include "pixel_fonts.h"
#include "terminal_ui.h"
#include "terminal_pixels.h"
#include "cyd_hal.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace cm {
namespace {

constexpr uint16_t rgb(unsigned colour) {
    return ((colour >> 19) & 31) << 11 | ((colour >> 10) & 63) << 5 | ((colour >> 3) & 31);
}

constexpr struct FontDef {
    unsigned cell_w, cell_h;
    unsigned cols, rows;
    const cyd_fonts::Font* font;
} font_defs[3] = {
    {6, 10, 53, 21, &cyd_fonts::pixelfont_7x5_thin},
    {8, 14, 40, 15, &cyd_fonts::pixelfont_11x5_thin},
    {10, 16, 32, 13, &cyd_fonts::pixelfont_13x7_semibold}
};

std::atomic<unsigned> active_font_idx{1}; // Default medium 40x15
std::atomic<bool> is_light_mode{false};

struct View {
    TerminalStorage storage{};
    TerminalParser parser;
    unsigned bytes = 0;
    bool diagnostic = false;
    uint64_t sync_since = 0;
    View(TerminalParser::Reply r, void* c, unsigned c_count, unsigned r_count)
        : parser(storage, r, c, c_count, r_count) {}
};

StaticSemaphore_t mutex_storage{};
SemaphoreHandle_t mutex = nullptr;
View* view = nullptr;

std::atomic<bool> clear_needed{true};
bool last_wifi = false, last_kb = false;
char last_host[32]{};
unsigned last_history = 0;
terminal::ui::Scene* ui_scene=nullptr;

struct Guard {
    bool held = false;
    Guard() { held = mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE; }
    ~Guard() { if (held) xSemaphoreGive(mutex); }
};

} // namespace

TerminalGeometry terminal_geometry() {
    const auto& def = font_defs[active_font_idx.load()];
    return {def.cols, def.rows, def.cell_w, def.cell_h};
}

unsigned terminal_font_index() {
    return active_font_idx.load();
}

void terminal_set_font_index(unsigned index) {
    if (index > 2) index = 2;
    active_font_idx.store(index);
}

void terminal_set_light_mode(bool light) {
    if (is_light_mode.load() != light) {
        is_light_mode.store(light);
        clear_needed = true;
    }
}
void terminal_use_ui_scene(terminal::ui::Scene* scene){ui_scene=scene;clear_needed=true;}

bool terminal_zoom(int direction) {
    if (!direction) return false;
    const unsigned current = active_font_idx.load();
    const int target = static_cast<int>(current) + (direction > 0 ? 1 : -1);
    const unsigned next = static_cast<unsigned>(std::clamp(target, 0, 2));
    if (current == next) return true;

    auto* scratch = static_cast<TerminalCell*>(heap_caps_malloc(terminal_cells * sizeof(TerminalCell), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!scratch) return false;

    bool resized = false;
    {
        Guard guard;
        if (guard.held && view) {
            const auto& def = font_defs[next];
            resized = view->parser.resize(def.cols, def.rows, scratch);
            if (resized) {
                active_font_idx.store(next);
                clear_needed = true;
            }
        }
    }
    heap_caps_free(scratch);
    if (resized) {
        const auto geo = terminal_geometry();
        std::printf("TM_ZOOM font_idx=%u cols=%u rows=%u cell=%ux%u\n",
                    next, geo.columns, geo.rows, geo.font_width, geo.font_height);
    }
    return resized;
}

bool terminal_begin(TerminalParser::Reply reply, void* context) {
    if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    Guard guard;
    if (!guard.held || view) return false;

    void* alloc = heap_caps_malloc(sizeof(View), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!alloc) return false;

    const auto& def = font_defs[active_font_idx.load()];
    view = new (alloc) View(reply, context, def.cols, def.rows);
    clear_needed = true;
    std::printf("TM_TERMINAL_BEGIN cols=%u rows=%u\n", def.cols, def.rows);
    return true;
}

bool terminal_end() {
    if (!mutex) return true;
    Guard guard;
    if (!guard.held) return false;
    if (view) {
        view->~View();
        heap_caps_free(view);
        view = nullptr;
    }
    clear_needed = true;
    std::printf("TM_TERMINAL_END\n");
    return true;
}

bool terminal_feed(const uint8_t* data, size_t size) {
    Guard guard;
    if (!guard.held || !view) return false;
    const auto clear_gen = view->parser.clear_generation();
    view->parser.feed(data, size);
    if (clear_gen != view->parser.clear_generation()) {
        clear_needed = true;
        view->parser.redraw();
    }
    view->bytes += static_cast<unsigned>(size);
    return true;
}

bool terminal_application_cursor() {
    Guard guard;
    return guard.held && view && view->parser.application_cursor();
}

void terminal_redraw() {
    Guard guard;
    if (guard.held && view) {
        clear_needed = true;
        view->parser.redraw();
    }
}

bool terminal_reset() {
    Guard guard;
    if (!guard.held || !view) return false;
    view->parser.reset();
    view->bytes = 0;
    clear_needed = true;
    return true;
}

bool terminal_check_colours() { return true; }
void terminal_scroll(int rows) {
    Guard guard;
    if (guard.held && view) view->parser.scroll_view(rows);
}
void terminal_live() { terminal_scroll(-2147483647); }
void terminal_metrics() {}

static esp_err_t render_header_bar(bool light,bool wifi_ok,bool kb_ok,const char* host,unsigned history) {
    if(!ui_scene)return ESP_ERR_INVALID_STATE;
    terminal::ui::build_header(*ui_scene,light,wifi_ok,kb_ok,host,history);
    return cyd::hal_render_region(0,0,320,26,terminal::ui::pixel,ui_scene);
}

esp_err_t terminal_render(unsigned budget, bool wifi_connected, bool keyboard_connected, const char* host_label) {
    if (!host_label) host_label = "SSH";
    const uint64_t started = esp_timer_get_time();
    const bool light = is_light_mode.load();

    bool need_full_clear = clear_needed.exchange(false);
    bool need_header = clear_needed || (wifi_connected != last_wifi) || (keyboard_connected != last_kb) ||
                       (std::strncmp(last_host, host_label, sizeof(last_host) - 1) != 0);

    {
        Guard guard;
        if (!guard.held || !view) return ESP_ERR_INVALID_STATE;
        if (view->parser.synchronized_output()) {
            if (!view->sync_since) view->sync_since = started;
            if (started - view->sync_since < 250000) {if(need_full_clear)clear_needed=true;return ESP_OK;}
        } else {
            view->sync_since = 0;
        }
        if (view->parser.view_offset() != last_history) {
            need_header = true;
            last_history = view->parser.view_offset();
        }
    }

    if (need_full_clear) {
        uint16_t blank_bg = light ? rgb(0xa9dcc2) : rgb(0x081115);
        const auto code=cyd::hal_render_region(0,26,320,214,
            [](int,int,void* context){return *static_cast<uint16_t*>(context);},&blank_bg);
        if(code!=ESP_OK){clear_needed=true;return code;}
        need_header = true;
    }

    if (need_header) {
        last_wifi = wifi_connected;
        last_kb = keyboard_connected;
        std::snprintf(last_host, sizeof(last_host), "%s", host_label);
        const auto code=render_header_bar(light,wifi_connected,keyboard_connected,host_label,last_history);
        if(code!=ESP_OK){clear_needed=true;return code;}
    }

    const auto& def = font_defs[active_font_idx.load()];
    const auto& font = *def.font;
    const unsigned cols = def.cols;
    const unsigned cell_w = def.cell_w;
    const unsigned cell_h = def.cell_h;
    const unsigned span_w = cols * cell_w;
    const unsigned x_start = (320 - span_w) / 2;


    TerminalCell cells[64];

    const unsigned limit = def.rows;
    for (unsigned step = 0; step < limit; ++step) {
        unsigned row_idx = 0;
        int cursor_col = -1;
        {
            Guard guard;
            if (!guard.held || !view) return ESP_ERR_INVALID_STATE;
            if (!view->parser.take_dirty_row(row_idx, cells, cols, cursor_col)) {
                break;
            }
        }

        if (row_idx >= def.rows) continue;

        // Paint directly into HAL's existing bounded DMA strips. The app
        // needs only a snapshot of one text row, not duplicate pixel buffers.
        struct RowContext {TerminalCell* cells;const cyd_fonts::Font* font;
            unsigned width,height,x,y;int cursor;bool light;};
        RowContext row{cells,&font,cell_w,cell_h,x_start,26+row_idx*cell_h,cursor_col,light};
        const auto code=cyd::hal_render_region(x_start,row.y,span_w,cell_h,
            [](int x,int y,void* context){const auto& r=*static_cast<RowContext*>(context);
                const unsigned px=static_cast<unsigned>(x)-r.x,column=px/r.width;
                return terminal::paint_pixel(r.cells[column],static_cast<int>(column)==r.cursor,
                    *r.font,r.width,r.height,px%r.width,static_cast<unsigned>(y)-r.y,r.light);},&row);
        if(code!=ESP_OK){terminal_redraw();return code;}
        if(static_cast<unsigned>(esp_timer_get_time()-started)>=budget)break;

    }

    return ESP_OK;
}

} // namespace cm
