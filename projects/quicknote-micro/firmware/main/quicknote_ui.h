#pragma once
#include "pixel_fonts.h"
#include <cstddef>
#include <cstdint>

namespace quicknote::ui {
enum class Kind:uint8_t {rectangle,text,clock,bluetooth};
struct Element {
    Kind kind=Kind::rectangle;int x=0,y=0,w=0,h=0;uint16_t colour=0;
    const cyd_fonts::Font* font=nullptr;int scale=1;char text[64]{};
};
struct Scene {Element elements[56]{};unsigned count=0;uint16_t background=0;};
struct Model {
    bool light=false,history=false,clock_valid=false,dirty=false,save_enabled=false;
    bool bluetooth_connected=false,button_clear_armed=false;
    int font_index=1,history_scroll=0,day_offset=0;
    const char* time="--:--",*date="Date unavailable",*bluetooth="BT WAIT",*status="";
    const char* content="";size_t content_size=0,cursor=0;
};
void build(Scene& scene,const Model& model);
uint16_t pixel(int x,int y,void* context);
unsigned visible_rows(int font_index);
}
