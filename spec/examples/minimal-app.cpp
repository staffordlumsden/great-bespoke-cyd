// Educational scaffold. Original ESP32; PixelFonts notices retained in release.
#include "console_bridge.h"
#include "cyd_hal.h"
#include "cyd_clock.h"
#include "pixel_fonts.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

namespace {
bool light=false;
constexpr uint16_t rgb(unsigned c) {return ((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31);}
bool ink(const char* text,int left,int top,int x,int y,const cyd_fonts::Font& f) {
    int cursor=left;
    for(const unsigned char* c=reinterpret_cast<const unsigned char*>(text);*c;++c) {
        const auto& g=f.glyphs[(*c>=f.first&&*c<=f.last?*c:'?')-f.first];
        const int gx=x-cursor-g.x,gy=y-top-f.ascent-g.y;
        if(gx>=0&&gy>=0&&gx<g.width&&gy<g.height) {
            const unsigned bit=gy*g.width+gx;
            if(f.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))return true;
        }
        cursor+=g.advance+1;
        if(cursor>x)break;
    }
    return false;
}
uint16_t pixel(int x,int y,void*) {
    const auto bg=rgb(light?0xa9dcc2:0x081115),surface=rgb(light?0xd6efe7:0x172b32);
    const auto fg=rgb(light?0x0c3d48:0xe4eef0),blue=rgb(light?0x147ba4:0x42a5f5);
    uint16_t colour=y<45?surface:bg;
    if(y>=185&&y<231&&x>=8&&x<312)colour=surface;
    if(ink("__TITLE__",8,12,x,y,cyd_fonts::pixelfont_13x7_semibold) ||
       ink("PixelFonts / shared CYD hardware",8,70,x,y,cyd_fonts::pixelfont_7x5_thin) ||
       ink("Touch controls below",8,100,x,y,cyd_fonts::pixelfont_11x5_thin))colour=fg;
    if(ink(light?"DARK":"LIGHT",24,202,x,y,cyd_fonts::pixelfont_11x5_thin) ||
       ink("CONSOLE",200,202,x,y,cyd_fonts::pixelfont_11x5_thin))colour=blue;
    return colour;
}
void save_theme() {
    nvs_handle_t handle{};
    if(nvs_open("__NVS__",NVS_READWRITE,&handle)==ESP_OK) {
        nvs_set_u8(handle,"light",light);nvs_commit(handle);nvs_close(handle);
    }
}
}
extern "C" void app_main() {
    esp_log_level_set("*",ESP_LOG_NONE);
    const auto nvs=nvs_flash_init(); // Never erase on initialization error.
    const auto board=cyd::hal_init();
    const bool healthy=nvs==ESP_OK && board.display==ESP_OK && board.touch==ESP_OK &&
                       board.sd==ESP_OK && board.fat32;
    if(nvs==ESP_OK) {
        cyd::clock::init();nvs_handle_t handle{};uint8_t saved=0;
        if(nvs_open("__NVS__",NVS_READONLY,&handle)==ESP_OK) {
            nvs_get_u8(handle,"light",&saved);nvs_close(handle);light=saved!=0;
        }
    }
    std::printf("EXAMPLE_BOARD healthy=%d accepted=NO\n",healthy);
    const bool first_frame=board.display==ESP_OK && cyd::hal_render(pixel)==ESP_OK;
    myapp::confirm_first_boot(healthy && first_frame);
    bool pressed=false;unsigned released=0;int tx=0,ty=0;
    for(;;) {
        int x=0,y=0;const bool down=board.touch==ESP_OK&&cyd::hal_touch_poll(x,y);
        if(down){if(!pressed){tx=x;ty=y;pressed=true;}released=0;}
        else if(pressed&&++released>=3) {
            pressed=false;
            if(ty>=185&&ty<231&&tx>=8&&tx<154) {
                light=!light;if(nvs==ESP_OK)save_theme();cyd::hal_render(pixel);
            } else if(ty>=185&&ty<231&&tx>=162&&tx<312) {
                cyd::hal_sd_unmount();
                if(!myapp::return_to_console())std::printf("EXAMPLE_CONSOLE unavailable\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
