#pragma once
#include "esp_err.h"
#include <cstdint>
#include <cstddef>

namespace cyd {

constexpr int DISPLAY_WIDTH = 320;
constexpr int DISPLAY_HEIGHT = 240;
constexpr int STRIP_ROWS = 8;

struct BoardStatus {
    esp_err_t display = ESP_FAIL;
    esp_err_t touch = ESP_FAIL;
    esp_err_t sd = ESP_FAIL;
    bool fat32 = false;
};

using PixelCallback = uint16_t(*)(int x, int y, void* user);

// Initialize GPIO, LCD, touch and SD card. Returns status without blocking.
BoardStatus hal_init();
// Configurable SD setup for transfer apps; existing Console defaults are unchanged.
BoardStatus hal_init_with_sd(unsigned clock_khz,unsigned open_files,unsigned transfer_bytes);

// Render the full 320x240 screen using the callback for each pixel.
// The callback is called row-by-row; implementation uses a small DMA strip buffer.
esp_err_t hal_render(PixelCallback cb, void* user = nullptr);
esp_err_t hal_render_region(int x,int y,int w,int h,PixelCallback cb,void* user=nullptr);

// Render a partial rectangle from a contiguous host-order RGB565 buffer.
// Implementation swaps LCD byte order. Caller must split into <= STRIP_ROWS
// rows; the DMA staging allocation is DISPLAY_WIDTH * STRIP_ROWS pixels.
esp_err_t hal_render_rect(int x, int y, int w, int h, const uint16_t* buffer);

// Poll for a single qualified touch point. Returns true if pressed and inside the screen.
bool hal_touch_poll(int& x, int& y);

// Mount the SD card at /sdcard. Safe to call repeatedly; returns ESP_OK if already mounted.
esp_err_t hal_sd_mount();
esp_err_t hal_sd_mount_config(unsigned clock_khz,unsigned open_files,unsigned transfer_bytes);

// Unmount the SD card.
esp_err_t hal_sd_unmount();

// Turn off/sleep the panel and hold its backlight low through ESP32 deep sleep.
// Resume also clears GPIO holds; normal hal_init does so after wake.
esp_err_t hal_display_suspend();
esp_err_t hal_display_resume();

// Return free internal heap in kilobytes.
unsigned hal_free_heap_kb();

// Return uptime in milliseconds.
uint64_t hal_uptime_ms();

// Return whether the SD card is currently mounted and readable.
bool hal_sd_ready();

// Read filesystem capacity/free bytes without opening or enumerating files.
bool hal_sd_usage(uint64_t& total, uint64_t& free);

} // namespace cyd
