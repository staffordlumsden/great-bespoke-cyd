#pragma once
#include "pixel_fonts.h"
#include <cstdint>
#include <cstddef>

namespace terminal::ui {
enum class Page:uint8_t {home,keyboard,wifi,hosts,session};
enum class Link:uint8_t {off,busy,ready,error};
enum class Action:uint8_t {none,theme,home,keyboard,wifi,hosts,connect,disconnect,console,
    bt_connect,bt_disconnect,bt_forget,wifi_retry,wifi_next,host_prev,host_next,host_select,
    zoom_out,zoom_in,live,resume};
enum class Kind:uint8_t {rectangle,text,terminal,bluetooth,wifi};
struct Element {
    Kind kind=Kind::rectangle;int x=0,y=0,w=0,h=0;uint16_t colour=0;
    const cyd_fonts::Font* font=nullptr;int scale=1;char text[80]{};
};
struct Target {int x=0,y=0,w=0,h=0;Action action=Action::none;unsigned index=0;bool enabled=true;};
// The fullest screen uses 29 elements. Keep spare capacity without reserving
// two large 64-element arrays alongside BLE/Wi-Fi and RSA signing buffers.
constexpr unsigned scene_capacity=32;
struct Scene {Element elements[scene_capacity]{};unsigned count=0;Target targets[12]{};unsigned target_count=0;uint16_t background=0;};
struct HostItem {char label[49]{};};
struct Model {
    Page page=Page::home;bool light=false,config_ok=false,session_active=false;
    Link wifi=Link::off,bluetooth=Link::off,ssh=Link::off;
    const char* wifi_status="OFF";const char* wifi_ssid="";const char* wifi_ip="";
    const char* bt_status="OFF";bool bt_saved=false,bt_found=false,bt_confirm=false;
    bool bt_passkey=false,forget_armed=false;const char* passkey="";unsigned keys=0;
    const char* host_label="No host";const char* host_address="";const char* host_user="";
    unsigned host_port=22,selected_host=0,host_count=0;HostItem hosts[8]{};
    const char* ssh_status="OFF";int error=0;const char* message="";
    unsigned free_ram_kb=0,font_index=1;const char* version="TM 0.2.3-headroom";
};
void build(Scene&,const Model&);
void build_header(Scene&,bool light,bool wifi,bool bt,const char* host,unsigned history);
uint16_t pixel(int x,int y,void*);
Target hit(const Scene&,int x,int y);
// Shared thick-line terminal icon for both menus and terminal chrome.
bool terminal_icon_ink(int x,int y,int w,int h);
}
