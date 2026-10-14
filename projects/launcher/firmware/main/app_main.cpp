#include "cyd_hal.h"
#include "cyd_clock.h"
#include "pixel_fonts.h"
#include "sd_installer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
struct Theme {
    uint16_t bg, surface, surface_hover, text_primary, text_muted, text_disabled;
};
constexpr Theme dark_theme{0x0861,0x2126,0x31a8,0xffff,0x9cf3,0x6b4d};
constexpr Theme light_theme{0xef7d,0xffff,0xd6ba,0x0841,0x52aa,0xa514};
constexpr uint16_t accent_blue=0x3b7f, accent_orange=0xfb40, accent_red=0xf186, accent_green=0x2625;
Theme theme=dark_theme;
bool dark_mode=true, wifi_on=false, bt_on=false, g_dirty=true;
enum class Page {home,library,confirm,progress,error,clock,set_time,shutdown};
Page page=Page::home;
cyd::clock::Civil editing_time{};
cyd::clock::Field editing_field=cyd::clock::Field::hour;
char clock_message[64]{};
constexpr unsigned library_limit=24, rows_per_page=3;
struct LibraryEntry {char name[101]{};cyd::FirmwareImage image{};char error[64]{};bool valid=false;};
LibraryEntry library[library_limit];
unsigned library_count=0, library_page=0, chosen=0;
bool library_truncated=false, installer_ready=false, has_installed=false;
char status_message[64]{}, install_stage[48]{};
unsigned install_percent=0;
bool sd_usage_valid=false;
uint64_t sd_total=0, sd_free=0;
uint16_t g_framebuffer[cyd::DISPLAY_WIDTH*cyd::DISPLAY_HEIGHT];
using cyd_fonts::Font;
using cyd_fonts::Glyph;

void draw_rect(int x,int y,int w,int h,uint16_t colour) {
    if(w<=0 || h<=0) return;
    const int right=std::min(x+w,cyd::DISPLAY_WIDTH), bottom=std::min(y+h,cyd::DISPLAY_HEIGHT);
    for(int row=std::max(y,0);row<bottom;++row)
        for(int col=std::max(x,0);col<right;++col)
            g_framebuffer[row*cyd::DISPLAY_WIDTH+col]=colour;
}
void draw_panel(int x,int y,int w,int h,uint16_t colour,uint16_t edge) {
    draw_rect(x,y,w,h,colour);
    if(y>=0 && y<cyd::DISPLAY_HEIGHT) draw_rect(x,y,w,1,edge);
}
const Glyph& font_glyph(const Font& face,char c) {
    const unsigned value=static_cast<unsigned char>(c);
    return face.glyphs[(value>=face.first && value<=face.last ? value : '?')-face.first];
}
int font_text_width(const char* text,const Font& face,int scale=1) {
    int width=0;
    for(;*text;++text) width+=(font_glyph(face,*text).advance+1)*scale;
    return width ? width-scale : 0;
}
void draw_font_text(const char* text,int x,int y,const Font& face,uint16_t colour,int scale=1) {
    const int baseline=y+face.ascent*scale;
    for(;*text;++text) {
        const auto& g=font_glyph(face,*text);
        for(unsigned gy=0;gy<g.height;++gy) for(unsigned gx=0;gx<g.width;++gx) {
            const unsigned bit=gy*g.width+gx;
            if(face.bitmap[g.offset+bit/8] & (0x80>>(bit%8)))
                draw_rect(x+(g.x+static_cast<int>(gx))*scale,
                          baseline+(g.y+static_cast<int>(gy))*scale,scale,scale,colour);
        }
        x+=(g.advance+1)*scale;
    }
}
void draw_font_centered(const char* text,int cx,int y,const Font& face,uint16_t colour) {
    draw_font_text(text,cx-font_text_width(text,face)/2,y,face,colour);
}
void draw_text(const char* text,int x,int y,uint16_t colour) {
    draw_font_text(text,x,y,cyd_fonts::pixelfont_7x5_thin,colour);
}
enum class Icon {appearance,wifi,bluetooth,power,sd,clock};
void draw_line(int x0,int y0,int x1,int y1,uint16_t colour) {
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    for(;;) {
        draw_rect(x0,y0,2,2,colour);
        if(x0==x1 && y0==y1) break;
        const int twice=2*error;
        if(twice>=dy) {error+=dy;x0+=sx;}
        if(twice<=dx) {error+=dx;y0+=sy;}
    }
}
void draw_icon(Icon icon,int x,int y,uint16_t colour) {
    if(icon==Icon::appearance) {
        for(int gy=0;gy<20;++gy) for(int gx=0;gx<20;++gx) {
            const int d=(gx-10)*(gx-10)+(gy-10)*(gy-10);
            if(d>=16 && d<=36) draw_rect(x+gx,y+gy,1,1,colour);
        }
        draw_line(x+10,y,x+10,y+2,colour);draw_line(x+10,y+18,x+10,y+19,colour);
        draw_line(x,y+10,x+2,y+10,colour);draw_line(x+18,y+10,x+19,y+10,colour);
        draw_line(x+3,y+3,x+4,y+4,colour);draw_line(x+16,y+3,x+17,y+2,colour);
        draw_line(x+3,y+17,x+4,y+16,colour);draw_line(x+16,y+16,x+17,y+17,colour);
    } else if(icon==Icon::wifi) {
        for(int gy=0;gy<15;++gy) for(int gx=0;gx<20;++gx) {
            const int d=(gx-10)*(gx-10)+(gy-18)*(gy-18);
            if((d>=225 && d<=289)||(d>=81 && d<=121)||(d>=16 && d<=36))
                draw_rect(x+gx,y+gy,1,1,colour);
        }
        draw_rect(x+9,y+17,3,3,colour);
    } else if(icon==Icon::bluetooth) {
        draw_line(x+9,y+1,x+9,y+18,colour);draw_line(x+9,y+1,x+15,y+6,colour);
        draw_line(x+15,y+6,x+3,y+15,colour);draw_line(x+3,y+4,x+15,y+13,colour);
        draw_line(x+15,y+13,x+9,y+18,colour);
    } else if(icon==Icon::power) {
        for(int gy=0;gy<20;++gy) for(int gx=0;gx<20;++gx) {
            const int d=(gx-10)*(gx-10)+(gy-10)*(gy-10);
            if(d>=49 && d<=81 && (gy>=7 || gx<7 || gx>13))
                draw_rect(x+gx,y+gy,1,1,colour);
        }
        draw_line(x+10,y+1,x+10,y+9,colour);
    } else if(icon==Icon::clock) {
        for(int gy=0;gy<20;++gy) for(int gx=0;gx<20;++gx) {
            const int d=(gx-10)*(gx-10)+(gy-10)*(gy-10);
            if(d>=64 && d<=81) draw_rect(x+gx,y+gy,1,1,colour);
        }
        draw_line(x+10,y+5,x+10,y+10,colour);
        draw_line(x+10,y+10,x+14,y+13,colour);
    } else {
        draw_line(x+6,y+1,x+17,y+1,colour);draw_line(x+17,y+1,x+17,y+18,colour);
        draw_line(x+17,y+18,x+3,y+18,colour);draw_line(x+3,y+18,x+3,y+5,colour);
        draw_line(x+3,y+5,x+6,y+1,colour);
        for(int pin=7;pin<16;pin+=3) draw_rect(x+pin,y+4,2,5,colour);
    }
}
struct Button {
    int x,y,w,h;
    const char* label;
    const char* sub;
    uint16_t bg=0,fg=0;
    bool enabled=true;
    const char* action="";
    Icon icon=Icon::appearance;
};
constexpr int button_count=5;
Button buttons[button_count]={
    {8,66,73,51,"Appearance","DARK",0,0,true,"APPEARANCE",Icon::appearance},
    {85,66,73,51,"Wi-Fi","OFF",0,0,true,"WIFI",Icon::wifi},
    {162,66,73,51,"Bluetooth","OFF",0,0,true,"BT",Icon::bluetooth},
    {239,66,73,51,"Shutdown",nullptr,0,0,true,"SHUTDOWN",Icon::power},
    {8,123,304,51,"Load from SD Card","Choose firmware",0,0,true,"LOAD_FIRMWARE",Icon::sd},
};
void update_button_labels() {
    buttons[0].sub=dark_mode?"DARK":"LIGHT";
    buttons[1].sub=wifi_on?"ON":"OFF";
    buttons[2].sub=bt_on?"ON":"OFF";
    for(auto& b:buttons) {
        b.bg=b.icon==Icon::sd?accent_orange:b.icon==Icon::power?accent_red:theme.surface;
        b.fg=(b.icon==Icon::sd || b.icon==Icon::power)?0xffff:theme.text_primary;
    }
}
void update_sd_usage() {sd_usage_valid=cyd::hal_sd_usage(sd_total,sd_free);}
void draw_header() {
    draw_font_text("CYD Console",12,6,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary,2);
    auto clock=cyd::clock::snapshot();
    draw_text(clock.valid?clock.date:"Tap clock to set date and time",12,36,theme.text_muted);
    char status[40];
    std::snprintf(status,sizeof(status),"%.23s  SD %s",esp_app_get_description()->version,cyd::hal_sd_ready()?"OK":"NONE");
    draw_text(status,12,48,theme.text_muted);
    draw_panel(214,6,98,26,theme.surface,accent_blue);
    char value[12];if(clock.valid){std::snprintf(value,sizeof(value),"%02d:%02d",clock.civil.hour,clock.civil.minute);draw_font_text(value,225,11,cyd_fonts::pixelfont_7x5_thin,theme.text_primary,2);}
    else draw_font_centered("SET TIME",263,13,cyd_fonts::pixelfont_11x5_thin,theme.text_primary);
    draw_panel(214,36,46,22,theme.surface,wifi_on?accent_green:accent_red);
    draw_text("WiFi",220,41,theme.text_muted);draw_rect(250,43,6,6,wifi_on?accent_green:accent_red);
    draw_panel(266,36,46,22,theme.surface,bt_on?accent_green:accent_red);
    draw_text("BT",272,41,theme.text_muted);draw_rect(300,43,6,6,bt_on?accent_green:accent_red);
}
void draw_button(const Button& b,bool pressed) {
    const auto bg=pressed?theme.surface_hover:b.bg;
    draw_panel(b.x,b.y,b.w,b.h,bg,pressed?accent_blue:theme.surface_hover);
    if(b.icon==Icon::sd) {
        draw_icon(b.icon,b.x+23,b.y+15,b.fg);
        draw_font_centered(b.label,b.x+174,b.y+11,cyd_fonts::pixelfont_13x7_semibold,b.fg);
        draw_font_centered(b.sub,b.x+174,b.y+31,cyd_fonts::pixelfont_7x5_thin,b.fg);
    } else {
        const uint16_t icon_colour=b.icon==Icon::power?b.fg:
            b.icon==Icon::appearance?(dark_mode?0xfec0:0x9860):accent_blue;
        draw_icon(b.icon,b.x+(b.w-20)/2,b.y+4,icon_colour);
        draw_font_centered(b.label,b.x+b.w/2,b.y+27,cyd_fonts::pixelfont_11x5_thin,b.fg);
        if(b.sub) draw_font_centered(b.sub,b.x+b.w/2,b.y+43,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    }
}
void draw_sd_usage() {
    draw_panel(8,180,304,44,theme.surface,theme.surface_hover);
    draw_font_text("SD Card",16,185,cyd_fonts::pixelfont_11x5_thin,theme.text_primary);
    if(!sd_usage_valid) {
        draw_text(cyd::hal_sd_ready()?"Usage unavailable":"No card mounted",110,187,theme.text_muted);
        return;
    }
    const uint64_t used=sd_total-sd_free;
    char size[40];
    std::snprintf(size,sizeof(size),"%llu.%llu / %llu.%llu GB",
        static_cast<unsigned long long>(used/1000000000),
        static_cast<unsigned long long>((used%1000000000)/100000000),
        static_cast<unsigned long long>(sd_total/1000000000),
        static_cast<unsigned long long>((sd_total%1000000000)/100000000));
    draw_font_centered(size,220,187,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    const unsigned percent=static_cast<unsigned>(used*100/sd_total);
    draw_rect(16,207,246,9,theme.surface_hover);
    draw_rect(16,207,static_cast<int>(used*246/sd_total),9,accent_blue);
    char fraction[8];
    std::snprintf(fraction,sizeof(fraction),"%u%%",percent);
    draw_text(fraction,273,208,theme.text_muted);
}
void draw_home() {
    draw_rect(0,0,320,240,theme.bg);draw_header();
    for(const auto& b:buttons) draw_button(b,false);
    draw_sd_usage();
    draw_panel(8,230,304,9,theme.surface,theme.surface_hover);
    draw_font_centered("Tailscale Settings - Coming Later",160,231,cyd_fonts::pixelfont_7x5_thin,theme.text_disabled);
}
void fitted_text(const char* text,int x,int y,int width,const Font& font,uint16_t colour) {
    char clipped[104];std::snprintf(clipped,sizeof(clipped),"%s",text);
    size_t length=std::strlen(clipped);
    const bool overflow=font_text_width(clipped,font)>width;
    while(length && font_text_width(clipped,font)>width)clipped[--length]=0;
    if(overflow && length>=3){clipped[length-1]='.';clipped[length-2]='.';clipped[length-3]='.';}
    draw_font_text(clipped,x,y,font,colour);
}
void flat_button(int x,int y,int w,int h,const char* label,bool enabled=true,uint16_t colour=accent_blue) {
    draw_panel(x,y,w,h,enabled?colour:theme.surface,theme.surface_hover);
    draw_font_centered(label,x+w/2,y+(h-11)/2,cyd_fonts::pixelfont_11x5_thin,enabled?0xffff:theme.text_disabled);
}
void page_heading(const char* title,Icon icon=Icon::sd) {
    draw_rect(0,0,320,240,theme.bg);
    draw_icon(icon,10,10,accent_orange);
    draw_font_text(title,40,12,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
}
void draw_library() {
    page_heading("SD Apps");
    flat_button(208,6,104,30,"Run last",has_installed);
    draw_text("/firmware on SD card",12,38,theme.text_muted);
    if(!library_count) {
        draw_font_text("No apps found",16,75,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
        fitted_text(status_message,16,100,288,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
        draw_text("Add application .bin files to /firmware",16,121,theme.text_muted);
        draw_text("Launcher stays in internal flash",16,145,theme.text_muted);
    }
    for(unsigned row=0;row<rows_per_page;++row){
        const unsigned index=library_page*rows_per_page+row;if(index>=library_count)break;
        const auto& entry=library[index];const int y=54+row*44;
        draw_panel(8,y,304,40,theme.surface,entry.valid?accent_blue:accent_red);
        const char* title=entry.valid?entry.image.project:entry.name;
        fitted_text(title,16,y+5,284,cyd_fonts::pixelfont_13x7_semibold,entry.valid?theme.text_primary:accent_red);
        char detail[72];
        if(entry.valid)std::snprintf(detail,sizeof(detail),"%.31s  %lu KB",entry.image.version,static_cast<unsigned long>(entry.image.size/1024));
        else std::snprintf(detail,sizeof(detail),"%s",entry.error);
        fitted_text(detail,16,y+25,284,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    }
    char count[64];std::snprintf(count,sizeof(count),"%u app%s  Page %u/%u%s",library_count,library_count==1?"":"s",library_page+1,std::max(1u,(library_count+2)/3),library_truncated?" (first 24)":"");
    draw_text(count,12,190,theme.text_muted);
    flat_button(8,207,96,28,"Back");
    flat_button(112,207,96,28,"Previous",library_page>0);
    flat_button(216,207,96,28,"Next",(library_page+1)*3<library_count);
}
void draw_confirmation() {
    page_heading("Install & run");const auto& entry=library[chosen];
    fitted_text(entry.image.project,12,49,296,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
    fitted_text(entry.image.version,12,72,296,cyd_fonts::pixelfont_11x5_thin,accent_blue);
    fitted_text(entry.name,12,95,296,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    char size[48];std::snprintf(size,sizeof(size),"ESP32 / %lu KB",static_cast<unsigned long>(entry.image.size/1024));
    draw_text(size,12,118,theme.text_muted);
    draw_text("The full image will be checked first",12,143,theme.text_muted);
    draw_text("Launcher and previous app are kept",12,159,theme.text_muted);
    draw_text("Keep power connected during install",12,178,theme.text_muted);
    flat_button(8,207,148,28,"Cancel");flat_button(164,207,148,28,"Install + run",true,accent_orange);
}
void draw_progress() {
    page_heading("Loading app");
    draw_icon(Icon::sd,149,62,accent_orange);
    draw_font_centered(install_stage,160,103,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
    draw_rect(24,138,272,14,theme.surface_hover);
    draw_rect(24,138,272*std::min(100u,install_percent)/100,14,accent_orange);
    char percent[12];std::snprintf(percent,sizeof(percent),"%u%%",install_percent);
    draw_font_centered(percent,160,167,cyd_fonts::pixelfont_11x5_thin,theme.text_primary);
    draw_font_centered("Keep power connected",160,206,cyd_fonts::pixelfont_11x5_thin,theme.text_muted);
}
void draw_error() {
    page_heading("App not started");
    fitted_text(status_message,12,75,296,cyd_fonts::pixelfont_11x5_thin,accent_red);
    draw_text("The launcher remains available",12,115,theme.text_muted);
    flat_button(8,207,148,28,"Console");flat_button(164,207,148,28,"SD Apps");
}
void draw_clock() {
    page_heading("Clock",Icon::clock);auto clock=cyd::clock::snapshot();
    char time[16];std::snprintf(time,sizeof(time),clock.valid?"%02d:%02d:%02d":"--:--:--",clock.civil.hour,clock.civil.minute,clock.civil.second);
    draw_font_text(time,88,51,cyd_fonts::pixelfont_7x5_thin,theme.text_primary,3);
    draw_font_centered(clock.valid?clock.date:"Date and time not set",160,86,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
    char zone[40];std::snprintf(zone,sizeof(zone),"Zone: %s",cyd::clock::zone_name(clock.zone));
    flat_button(8,111,148,30,zone);
    flat_button(164,111,148,30,clock.automatic?"Auto sync ON":"Auto sync OFF");
    flat_button(8,151,148,35,"Set manually");
    flat_button(164,151,148,35,"Wi-Fi sync",cyd::clock_helper_available());
    fitted_text(clock_message[0]?clock_message:"Clock keeps time while power stays on",12,194,296,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    flat_button(8,211,304,27,"Back");
}
void draw_time_editor() {
    page_heading("Set local time",Icon::clock);
    draw_text(cyd::clock::zone_name(cyd::clock::snapshot().zone),12,36,theme.text_muted);
    const char* names[]={"Year","Month","Day","Hour","Minute","Second"};
    const int values[]={editing_time.year,editing_time.month,editing_time.day,editing_time.hour,editing_time.minute,editing_time.second};
    for(unsigned i=0;i<6;++i){int x=8+(i%3)*104,y=52+(i/3)*56;draw_panel(x,y,96,50,theme.surface,unsigned(editing_field)==i?accent_blue:theme.surface_hover);draw_font_centered(names[i],x+48,y+5,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);char value[8];std::snprintf(value,sizeof(value),i?"%02d":"%04d",values[i]);draw_font_text(value,x+(96-font_text_width(value,cyd_fonts::pixelfont_7x5_thin,2))/2,y+22,cyd_fonts::pixelfont_7x5_thin,theme.text_primary,2);}
    flat_button(8,166,148,29,"- Lower");flat_button(164,166,148,29,"+ Higher");
    fitted_text(clock_message[0]?clock_message:"Select a field, then use - / +",12,200,296,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
    flat_button(8,213,148,25,"Cancel");flat_button(164,213,148,25,"Save",true,accent_green);
}
void draw_screen() {
    switch(page){
    case Page::home:draw_home();break;
    case Page::library:draw_library();break;
    case Page::confirm:draw_confirmation();break;
    case Page::progress:draw_progress();break;
    case Page::error:draw_error();break;
    case Page::shutdown:
        draw_rect(0,0,320,240,theme.bg);
        draw_icon(Icon::power,150,55,accent_red);
        draw_font_centered("Shutting down",160,93,cyd_fonts::pixelfont_13x7_semibold,theme.text_primary);
        draw_font_centered("Tap screen to wake",160,131,cyd_fonts::pixelfont_11x5_thin,theme.text_muted);
        draw_font_centered("Release your finger",160,179,cyd_fonts::pixelfont_7x5_thin,theme.text_muted);
        break;
    case Page::clock:draw_clock();break;
    case Page::set_time:draw_time_editor();break;
    }
}
uint16_t pixel_callback(int x,int y,void*) {return g_framebuffer[y*320+x];}
void render() {
    draw_screen();
    const auto r=cyd::hal_render(pixel_callback,nullptr);
    if(r!=ESP_OK) std::printf("LAUNCHER render_error=%s\n",esp_err_to_name(r));
    g_dirty=false;
}
int hit_button(int x,int y) {
    for(int i=0;i<button_count;++i) {
        const auto& b=buttons[i];
        if(b.enabled && x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h) return i;
    }
    return -1;
}
void shutdown_console() {
    page=Page::shutdown;render();vTaskDelay(pdMS_TO_TICKS(700));
    const uint64_t deadline=cyd::hal_uptime_ms()+5000;
    unsigned released=0;
    while(released<10&&cyd::hal_uptime_ms()<deadline){
        released=gpio_get_level(GPIO_NUM_36)?released+1:0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if(released<10){page=Page::error;std::snprintf(status_message,sizeof(status_message),"Release screen and try Shutdown again");g_dirty=true;return;}
    auto code=esp_sleep_enable_ext0_wakeup(GPIO_NUM_36,0);
    const bool had_sd=cyd::hal_sd_ready();
    if(code==ESP_OK)code=cyd::hal_sd_unmount();
    if(code==ESP_OK)code=cyd::hal_display_suspend();
    if(code!=ESP_OK){
        cyd::hal_display_resume();if(had_sd)cyd::hal_sd_mount();
        page=Page::error;std::snprintf(status_message,sizeof(status_message),"Shutdown: %s",esp_err_to_name(code));g_dirty=true;
        std::printf("POWER state=ERROR code=%d\n",code);return;
    }
    std::printf("POWER state=DEEP_SLEEP wake=TOUCH sd=UNMOUNTED nvs=PRESERVED\n");std::fflush(stdout);
    esp_deep_sleep_start();
}
void run_action(const char* action) {
    std::printf("LAUNCHER action=%s uptime=%llu heap_kb=%u\n",action,
        static_cast<unsigned long long>(cyd::hal_uptime_ms()),cyd::hal_free_heap_kb());
    if(std::strcmp(action,"APPEARANCE")==0) {
        dark_mode=!dark_mode;theme=dark_mode?dark_theme:light_theme;
        std::printf("LAUNCHER theme=%s\n",dark_mode?"DARK":"LIGHT");
    } else if(std::strcmp(action,"WIFI")==0) wifi_on=!wifi_on;
    else if(std::strcmp(action,"BT")==0) bt_on=!bt_on;
    else if(std::strcmp(action,"LOAD_FIRMWARE")==0) page=Page::library;
    else if(std::strcmp(action,"SHUTDOWN")==0) shutdown_console();
    update_button_labels();g_dirty=true;
}
void show_firmware_list() {
    library_count=library_page=0;library_truncated=false;status_message[0]=0;
    cyd::InstalledApp last;has_installed=cyd::installed_app(last);
    if(!installer_ready){page=Page::error;std::snprintf(status_message,sizeof(status_message),"USB layout migration needed");return;}
    page=Page::library;
    if(!cyd::hal_sd_ready()){std::snprintf(status_message,sizeof(status_message),"No SD card mounted");return;}
    DIR* directory=opendir("/sdcard/firmware");
    if(!directory){std::snprintf(status_message,sizeof(status_message),"No /firmware folder on this card");return;}
    esp_chip_info_t chip{};esp_chip_info(&chip);
    while(auto* entry=readdir(directory)) {
        const char* name=entry->d_name;
        if(!cyd::firmware_filename(name))continue;
        if(library_count==library_limit){library_truncated=true;continue;}
        char path[128];struct stat details{};
        std::snprintf(path,sizeof(path),"/sdcard/firmware/%.100s",name);
        if(stat(path,&details)!=0 || !S_ISREG(details.st_mode))continue;
        auto& item=library[library_count++];item={};std::snprintf(item.name,sizeof(item.name),"%.100s",name);
        FILE* file=std::fopen(path,"rb");
        item.valid=cyd::probe_firmware(file,cyd::app_slot_size,chip.revision,item.image,item.error);
        if(file)std::fclose(file);
    }
    closedir(directory);
    std::sort(library,library+library_count,[](const LibraryEntry& a,const LibraryEntry& b){return std::strcmp(a.name,b.name)<0;});
    std::printf("LOADER library count=%u last_app=%d truncated=%d\n",library_count,has_installed,library_truncated);
    update_sd_usage();g_dirty=true;
}
void install_progress(const char* stage,unsigned percent,void*) {
    const bool changed=std::strcmp(stage,install_stage)!=0;
    if(changed || percent==100 || percent>=install_percent+5){
        std::snprintf(install_stage,sizeof(install_stage),"%s",stage);install_percent=percent;
        render();std::printf("LOADER progress stage=%s percent=%u\n",stage,percent);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
}
TaskHandle_t install_caller=nullptr;
bool install_wants_last=false, install_wants_clock=false,install_ok=false;
void install_task(void*) {
    install_ok=install_wants_clock?cyd::boot_clock_helper(status_message):install_wants_last?cyd::boot_installed(status_message,install_progress):cyd::install_firmware(library[chosen].name,status_message,install_progress);
    std::printf("LOADER task_finished ok=%d stack_min=%u\n",install_ok,static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    xTaskNotifyGive(install_caller);vTaskDelete(nullptr);
}
void load_app(bool run_last) {
    page=Page::progress;install_stage[0]=0;install_percent=0;
    install_progress("Checking app",0,nullptr);
    install_caller=xTaskGetCurrentTaskHandle();install_wants_last=run_last;install_wants_clock=false;install_ok=false;
    // ESP32 releases its startup stack heaps only after the scheduler begins.
    // Allocate the install stack here, keeping the main startup stack at 4 KB.
    if(xTaskCreate(install_task,"sd_install",16384,nullptr,2,nullptr)!=pdPASS){
        std::snprintf(status_message,sizeof(status_message),"Not enough RAM to install app");page=Page::error;g_dirty=true;return;
    }
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    if(!install_ok){page=Page::error;g_dirty=true;std::printf("LOADER failure=%s\n",status_message);return;}
    install_progress("Starting app",100,nullptr);std::fflush(stdout);
    cyd::hal_sd_unmount();vTaskDelay(pdMS_TO_TICKS(400));esp_restart();
}
bool start_clock_sync(const char* source) {
    if(!cyd::clock_helper_available()){std::snprintf(clock_message,sizeof(clock_message),"Install File Server clock update first");return false;}
    if(!cyd::clock::request_sync()){std::snprintf(clock_message,sizeof(clock_message),"Could not save clock sync request");return false;}
    install_caller=xTaskGetCurrentTaskHandle();install_wants_clock=true;install_ok=false;
    if(xTaskCreate(install_task,"clock_start",16384,nullptr,2,nullptr)!=pdPASS){cyd::clock::cancel_request();std::snprintf(clock_message,sizeof(clock_message),"Not enough RAM to start time sync");return false;}
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    if(!install_ok){cyd::clock::cancel_request();std::snprintf(clock_message,sizeof(clock_message),"%s",status_message);return false;}
    std::printf("CLOCK sync requested source=%s last_app=PRESERVED\n",source);std::fflush(stdout);
    cyd::hal_sd_unmount();esp_restart();return true;
}
void handle_touch(int x,int y) {
    if(page==Page::home){
        if(x>=214&&x<312&&y>=6&&y<32){page=Page::clock;clock_message[0]=0;g_dirty=true;return;}
        const int index=hit_button(x,y);
        if(index>=0){run_action(buttons[index].action);if(page==Page::library)show_firmware_list();}
    }else if(page==Page::library){
        if(y>=6 && y<36 && x>=208 && x<312 && has_installed)load_app(true);
        else if(x>=8 && x<312 && y>=54 && y<182){
            const unsigned row=(y-54)/44,index=library_page*3+row;
            if((y-54)%44<40 && index<library_count){chosen=index;if(library[index].valid)page=Page::confirm;else{std::snprintf(status_message,sizeof(status_message),"%s",library[index].error);page=Page::error;}}
        }else if(y>=207 && y<235){
            if(x>=8 && x<104)page=Page::home;
            else if(x>=112 && x<208 && library_page) --library_page;
            else if(x>=216 && x<312 && (library_page+1)*3<library_count)++library_page;
        }
    }else if(page==Page::confirm && y>=207 && y<235){
        if(x>=8 && x<156)page=Page::library;
        else if(x>=164 && x<312)load_app(false);
    }else if(page==Page::clock){
        if(y>=111&&y<141){if(x>=8&&x<156){auto zone=cyd::clock::snapshot().zone;cyd::clock::set_zone(static_cast<cyd::clock::Zone>((unsigned(zone)+1)%unsigned(cyd::clock::Zone::count)));}else if(x>=164&&x<312)cyd::clock::set_automatic(!cyd::clock::snapshot().automatic);}
        else if(y>=151&&y<186){if(x>=8&&x<156){auto c=cyd::clock::snapshot();editing_time=c.valid?c.civil:cyd::clock::Civil{};editing_field=cyd::clock::Field::hour;clock_message[0]=0;page=Page::set_time;}else if(x>=164&&x<312)start_clock_sync("TOUCH");}
        else if(y>=211&&y<238&&x>=8&&x<312)page=Page::home;
    }else if(page==Page::set_time){
        if(y>=52&&y<158&&x>=8&&x<312){unsigned column=(x-8)/104,row=(y-52)/56;if(column<3&&row<2&&(x-8)%104<96&&(y-52)%56<50)editing_field=static_cast<cyd::clock::Field>(row*3+column);}
        else if(y>=166&&y<195){if(x>=8&&x<156)cyd::clock::adjust(editing_time,editing_field,-1);else if(x>=164&&x<312)cyd::clock::adjust(editing_time,editing_field,1);clock_message[0]=0;}
        else if(y>=213&&y<238){if(x>=8&&x<156){page=Page::clock;clock_message[0]=0;}else if(x>=164&&x<312){if(cyd::clock::set_local(editing_time)){page=Page::clock;std::snprintf(clock_message,sizeof(clock_message),"Local date and time saved");}else std::snprintf(clock_message,sizeof(clock_message),"Invalid time (check daylight saving)");}}
    }else if(page==Page::error && y>=207 && y<235){
        if(x>=8 && x<156)page=Page::home;
        else if(x>=164 && x<312)show_firmware_list();
    }
    g_dirty=true;
}
} // namespace

extern "C" void app_main() {
    std::printf("\nCYD Launcher %s\n",esp_app_get_description()->version);
    const auto wake=esp_sleep_get_wakeup_cause();
    if(wake==ESP_SLEEP_WAKEUP_EXT0)rtc_gpio_deinit(GPIO_NUM_36);
    std::printf("POWER wake=%s\n",wake==ESP_SLEEP_WAKEUP_EXT0?"TOUCH":wake==ESP_SLEEP_WAKEUP_UNDEFINED?"RESET":"OTHER");
    const auto s=cyd::hal_init();
    std::printf("HAL init display=%s touch=%s sd=%s fat32=%d\n",
        esp_err_to_name(s.display),esp_err_to_name(s.touch),esp_err_to_name(s.sd),s.fat32);
    installer_ready=cyd::installer_init(status_message);
    cyd::clock::init();
    const auto return_result=cyd::clock::take_return_result();
    const bool recovery_hold=gpio_get_level(GPIO_NUM_36)==0;
    if(return_result!=cyd::clock::Result::none&&!recovery_hold){page=Page::clock;std::snprintf(clock_message,sizeof(clock_message),return_result==cyd::clock::Result::synced?"Wi-Fi time synchronized":return_result==cyd::clock::Result::unavailable?"No time server; set manually or retry":"Sync interrupted; set manually or retry");}
    update_button_labels();update_sd_usage();render();
    std::printf("LAUNCHER storage valid=%d total_bytes=%llu free_bytes=%llu\n",sd_usage_valid,
        static_cast<unsigned long long>(sd_total),static_cast<unsigned long long>(sd_free));
    g_dirty=true;
    std::printf("LOADER ready=%d recovery=TOUCH_HOLD_3S nvs=PRESERVED\n",installer_ready);
    auto current_clock=cyd::clock::snapshot();std::printf("CLOCK boot valid=%d zone=%s auto=%d recovery_hold=%d return=%u\n",current_clock.valid,cyd::clock::zone_name(current_clock.zone),current_clock.automatic,recovery_hold,unsigned(return_result));std::fflush(stdout);
    if(return_result==cyd::clock::Result::none&&!current_clock.valid&&current_clock.automatic&&!recovery_hold&&cyd::clock_helper_available())start_clock_sync("AUTO");
    int touch_x=0,touch_y=0;
    bool touching=false,touch_armed=false;
    unsigned idle_release_samples=0;
    unsigned release_samples=0;
    uint64_t last_uptime=0;
    for(;;) {
        int x=0,y=0;
        const bool pressed=cyd::hal_touch_poll(x,y);
        if(!touch_armed) {
            if(pressed) idle_release_samples=0;
            else if(++idle_release_samples>=10){touch_armed=true;release_samples=0;std::printf("LAUNCHER touch_armed=1\n");}
        } else if(pressed) {
            if(!touching) {touch_x=x;touch_y=y;touching=true;}
            release_samples=0;
        } else if(touching && ++release_samples>=3) {
            handle_touch(touch_x,touch_y);
            touching=false;g_dirty=true;
        }
        const uint64_t now=cyd::hal_uptime_ms();
        if(now/1000!=last_uptime/1000 && !touching) {last_uptime=now;if(page==Page::home||page==Page::clock)g_dirty=true;}
        if(g_dirty) render();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
