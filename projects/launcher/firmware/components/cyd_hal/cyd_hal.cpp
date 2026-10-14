#include "cyd_hal.h"
#include "sdkconfig.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdmmc_cmd.h"

namespace cyd {

namespace {

// Source: tested CYD2USB profile from the user's ESP32-2432S028R.
constexpr gpio_num_t lcd_mosi = GPIO_NUM_13, lcd_miso = GPIO_NUM_12;
constexpr gpio_num_t lcd_clock = GPIO_NUM_14, lcd_cs = GPIO_NUM_15;
constexpr gpio_num_t lcd_dc = GPIO_NUM_2, lcd_backlight = GPIO_NUM_21;
constexpr gpio_num_t touch_mosi = GPIO_NUM_32, touch_miso = GPIO_NUM_39;
constexpr gpio_num_t touch_clock = GPIO_NUM_25, touch_cs = GPIO_NUM_33;
constexpr gpio_num_t touch_irq = GPIO_NUM_36;
constexpr gpio_num_t sd_mosi = GPIO_NUM_23, sd_miso = GPIO_NUM_19;
constexpr gpio_num_t sd_clock = GPIO_NUM_18, sd_cs = GPIO_NUM_5;

constexpr int width = DISPLAY_WIDTH, height = DISPLAY_HEIGHT, strip_rows = STRIP_ROWS;
alignas(4) uint16_t strip_buffer[width * strip_rows];

esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t dma_done = nullptr;
sdmmc_card_t* sd_card = nullptr;
bool display_ready = false, touch_ready = false, sd_mounted = false, sd_fat32 = false;

// Measured on the user's unit, 9 October 2026.
struct TouchTransform { float x[3], y[3]; };
constexpr TouchTransform board_profile = {
    {2.8442044f, -181.8562317f, 149.8756104f},
    {-136.3600464f, -0.4024602f, 117.9305954f}
};

void project(const TouchTransform& t, int raw_x, int raw_y, float& sx, float& sy) {
    const float nx = (raw_x - 2048.0f) / 2048.0f;
    const float ny = (raw_y - 2048.0f) / 2048.0f;
    sx = t.x[0] * nx + t.x[1] * ny + t.x[2];
    sy = t.y[0] * nx + t.y[1] * ny + t.y[2];
}

bool colour_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
    return woken == pdTRUE;
}

esp_err_t init_display() {
    gpio_config_t bl{};
    bl.pin_bit_mask = 1ULL << lcd_backlight;
    bl.mode = GPIO_MODE_OUTPUT;
    esp_err_t r = gpio_config(&bl);
    if (r != ESP_OK) return r;
    gpio_set_level(lcd_backlight, 0);
    // Restore normal pad control after Console's deep-sleep shutdown. Configure
    // the low output first to avoid a backlight flash while releasing the hold.
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(lcd_backlight);

    spi_bus_config_t bus{};
    bus.mosi_io_num = lcd_mosi; bus.miso_io_num = lcd_miso; bus.sclk_io_num = lcd_clock;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
    bus.max_transfer_sz = sizeof(strip_buffer);
    r = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) return r;

    dma_done = xSemaphoreCreateBinary();
    if (!dma_done) return ESP_ERR_NO_MEM;

    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = lcd_cs; io.dc_gpio_num = lcd_dc;
    io.pclk_hz = 10 * 1000 * 1000;
    io.spi_mode = 0; io.trans_queue_depth = 1;
    io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    io.on_color_trans_done = colour_done; io.user_ctx = dma_done;

    esp_lcd_panel_io_handle_t io_handle = nullptr;
    r = esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &io_handle);
    if (r != ESP_OK) return r;

    esp_lcd_panel_dev_config_t dev{};
    dev.reset_gpio_num = -1;
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    dev.bits_per_pixel = 16;
    r = esp_lcd_new_panel_st7789(io_handle, &dev, &panel);
    if (r != ESP_OK) return r;

    if ((r = esp_lcd_panel_reset(panel)) != ESP_OK) return r;
    if ((r = esp_lcd_panel_init(panel)) != ESP_OK) return r;
    if ((r = esp_lcd_panel_invert_color(panel, false)) != ESP_OK) return r;
    if ((r = esp_lcd_panel_swap_xy(panel, true)) != ESP_OK) return r;
    if ((r = esp_lcd_panel_mirror(panel, true, true)) != ESP_OK) return r;
    {
        const uint8_t origin[] = {0, 0};
        if ((r = esp_lcd_panel_io_tx_param(io_handle, 0x37, origin, sizeof(origin))) != ESP_OK) return r;
    }
    if ((r = esp_lcd_panel_disp_on_off(panel, true)) != ESP_OK) return r;
    gpio_set_level(lcd_backlight, 1);
    return ESP_OK;
}

esp_err_t init_touch() {
    gpio_config_t out{};
    out.pin_bit_mask = (1ULL << touch_clock) | (1ULL << touch_mosi) | (1ULL << touch_cs);
    out.mode = GPIO_MODE_OUTPUT;
    esp_err_t r = gpio_config(&out);
    if (r != ESP_OK) return r;
    gpio_set_level(touch_cs, 1); gpio_set_level(touch_clock, 0); gpio_set_level(touch_mosi, 0);
    gpio_config_t in{};
    in.pin_bit_mask = (1ULL << touch_miso) | (1ULL << touch_irq);
    in.mode = GPIO_MODE_INPUT;
    return gpio_config(&in);
}

unsigned touch_adc(uint8_t cmd) {
    gpio_set_level(touch_cs, 0);
    esp_rom_delay_us(2);
    uint32_t reply = 0;
    const uint32_t out = static_cast<uint32_t>(cmd) << 16;
    for (int bit = 23; bit >= 0; --bit) {
        gpio_set_level(touch_mosi, (out >> bit) & 1U);
        esp_rom_delay_us(2);
        gpio_set_level(touch_clock, 1);
        esp_rom_delay_us(2);
        reply = (reply << 1) | static_cast<unsigned>(gpio_get_level(touch_miso));
        gpio_set_level(touch_clock, 0);
    }
    gpio_set_level(touch_cs, 1);
    return (reply >> 3) & 0xfffU;
}

} // namespace

BoardStatus hal_init() {
    return hal_init_with_sd(10000,2,512);
}

BoardStatus hal_init_with_sd(unsigned clock_khz,unsigned open_files,unsigned transfer_bytes) {
    BoardStatus s{};
    s.display = init_display();
    display_ready = s.display == ESP_OK;
    s.touch = init_touch();
    touch_ready = s.touch == ESP_OK;
    s.sd = hal_sd_mount_config(clock_khz,open_files,transfer_bytes);
    sd_mounted = s.sd == ESP_OK;
    s.fat32 = sd_fat32;
    return s;
}

esp_err_t hal_render(PixelCallback cb, void* user) {
    return hal_render_region(0,0,width,height,cb,user);
}

esp_err_t hal_render_region(int x,int y,int w,int h,PixelCallback cb,void* user) {
    if (!panel || !cb) return ESP_ERR_INVALID_STATE;
    const int right=std::min(width,x+w),bottom=std::min(height,y+h);
    x=std::max(0,x);y=std::max(0,y);w=right-x;h=bottom-y;
    if(w<=0 || h<=0)return ESP_ERR_INVALID_ARG;
    for (int top=y;top<bottom;top+=strip_rows) {
        const int rows=std::min(strip_rows,bottom-top);
        for (int row=0;row<rows;++row) {
            for (int col=0;col<w;++col) {
                const uint16_t c=cb(x+col,top+row,user);
                strip_buffer[row*w+col]=static_cast<uint16_t>((c << 8) | (c >> 8));
            }
        }
        esp_err_t r=esp_lcd_panel_draw_bitmap(panel,x,top,right,top+rows,strip_buffer);
        if (r != ESP_OK) return r;
        if (xSemaphoreTake(dma_done, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t hal_render_rect(int x, int y, int w, int h, const uint16_t* buffer) {
    if (!panel || !buffer || w <= 0 || h <= 0) return ESP_ERR_INVALID_ARG;
    if (x < 0) { buffer += -x; w += x; x = 0; }
    if (y < 0) { buffer += -y * w; h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return ESP_OK;
    // Convert byte order in-place into strip_buffer; caller owns source buffer.
    for (int row = 0; row < h; ++row) {
        const uint16_t* src = buffer + row * w;
        for (int col = 0; col < w; ++col) {
            const uint16_t c = src[col];
            strip_buffer[row * w + col] = static_cast<uint16_t>((c << 8) | (c >> 8));
        }
    }
    esp_err_t r = esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, strip_buffer);
    if (r != ESP_OK) return r;
    return xSemaphoreTake(dma_done, pdMS_TO_TICKS(2000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool hal_touch_poll(int& x, int& y) {
    if (!touch_ready || gpio_get_level(touch_irq) != 0) return false;
    const unsigned rx = touch_adc(0xd0), ry = touch_adc(0x90), z = touch_adc(0xb0);
    if (z <= 50 || !rx || rx >= 4095 || !ry || ry >= 4095) return false;
    float sx = 0, sy = 0;
    project(board_profile, rx, ry, sx, sy);
    if (!std::isfinite(sx) || !std::isfinite(sy)) return false;
    x = static_cast<int>(std::lround(sx));
    y = static_cast<int>(std::lround(sy));
    return x >= 0 && x < width && y >= 0 && y < height;
}

esp_err_t hal_sd_mount() {
    return hal_sd_mount_config(10000,2,512);
}

esp_err_t hal_sd_mount_config(unsigned clock_khz,unsigned open_files,unsigned transfer_bytes) {
    if (sd_mounted && sd_card) return ESP_OK;
    static bool bus_init = false;
    if (!bus_init) {
        spi_bus_config_t bus{};
        bus.mosi_io_num = sd_mosi; bus.miso_io_num = sd_miso;
        bus.sclk_io_num = sd_clock; bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
        bus.max_transfer_sz = transfer_bytes;
        esp_err_t r = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
        if (r != ESP_OK) return r;
        bus_init = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST; host.max_freq_khz = clock_khz;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI3_HOST; slot.gpio_cs = sd_cs;
    esp_vfs_fat_mount_config_t mount{};
    mount.format_if_mount_failed = false; mount.max_files = open_files;
    mount.disk_status_check_enable = true;
    esp_err_t r = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &sd_card);
    if (r != ESP_OK) return r;
    FF_DIR root{};
    FRESULT opened = f_opendir(&root, "0:/");
    if (opened == FR_OK) {
        sd_fat32 = (root.obj.fs->fs_type == FS_FAT32);
        f_closedir(&root);
    }
    sd_mounted = true;
    return ESP_OK;
}

esp_err_t hal_sd_unmount() {
    if (!sd_card) return ESP_OK;
    esp_err_t r = esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
    sd_card = nullptr;
    sd_mounted = false;
    return r;
}

unsigned hal_free_heap_kb() {
    return static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
}

esp_err_t hal_display_suspend() {
    if(!panel)return ESP_ERR_INVALID_STATE;
    auto code=gpio_set_level(lcd_backlight,0);
    if(code==ESP_OK)code=esp_lcd_panel_disp_on_off(panel,false);
    if(code==ESP_OK)code=esp_lcd_panel_disp_sleep(panel,true);
    if(code==ESP_OK)code=gpio_hold_en(lcd_backlight);
    if(code==ESP_OK)gpio_deep_sleep_hold_en();
    return code;
}

esp_err_t hal_display_resume() {
    if(!panel)return ESP_ERR_INVALID_STATE;
    gpio_set_level(lcd_backlight,0);
    gpio_deep_sleep_hold_dis();
    auto code=gpio_hold_dis(lcd_backlight);
    if(code==ESP_OK)code=esp_lcd_panel_disp_sleep(panel,false);
    if(code==ESP_OK)code=esp_lcd_panel_disp_on_off(panel,true);
    if(code==ESP_OK)code=gpio_set_level(lcd_backlight,1);
    return code;
}

uint64_t hal_uptime_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

bool hal_sd_ready() { return sd_mounted; }

bool hal_sd_usage(uint64_t& total, uint64_t& free) {
    total=free=0;
    return sd_mounted && esp_vfs_fat_info("/sdcard", &total, &free)==ESP_OK &&
           total>0 && free<=total;
}

} // namespace cyd
