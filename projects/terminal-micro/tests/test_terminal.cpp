#include "terminal_ui.h"
#include "terminal_pixels.h"
#include "terminal_renderer.h"
#include "touch_gesture.h"
#include "cyd_hal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <array>

std::array<uint16_t,320*240> lcd{};
unsigned transfers=0;
namespace cyd {
esp_err_t hal_render_region(int x,int y,int w,int h,PixelCallback cb,void* u){
 assert(x>=0&&y>=0&&x+w<=320&&y+h<=240);for(int strip=y;strip<y+h;strip+=8){for(int yy=strip;yy<std::min(strip+8,y+h);++yy)for(int xx=x;xx<x+w;++xx)lcd[yy*320+xx]=cb(xx,yy,u);++transfers;}return ESP_OK;
}
esp_err_t hal_render_rect(int x,int y,int w,int h,const uint16_t* data){
 // Real HAL uses one 320x8 DMA strip; catch the original oversized writes.
 assert(h<=8&&w*h<=320*8&&x>=0&&y>=0&&x+w<=320&&y+h<=240);
 for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)lcd[(y+yy)*320+x+xx]=data[yy*w+xx];++transfers;return ESP_OK;
}
}
void ppm(const std::string& name){auto* f=std::fopen(name.c_str(),"wb");assert(f);std::fprintf(f,"P6\n320 240\n255\n");for(auto c:lcd){unsigned char p[]={static_cast<unsigned char>((c>>11&31)*255/31),static_cast<unsigned char>((c>>5&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(p,1,3,f);}std::fclose(f);}
int main(int argc,char** argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);using namespace terminal::ui;
 Model m{};m.config_ok=true;m.wifi=Link::ready;m.wifi_status="CONNECTED";m.bluetooth=Link::off;m.host_label="Workbench";m.host_count=2;m.free_ram_kb=90;
 std::strcpy(m.hosts[0].label,"Workbench");std::strcpy(m.hosts[1].label,"Lab Server");
 for(int mode=0;mode<2;++mode)for(unsigned p=0;p<5;++p){
  m.light=mode;m.page=Page(p);Scene s{};build(s,m);
  assert(s.count<scene_capacity); // Do not hide clipped UI after shrinking scene storage.
  for(unsigned i=0;i<s.count;++i){auto& e=s.elements[i];assert(e.x>=0&&e.y>=0&&e.x+e.w<=320&&e.y+e.h<=240);}
  for(unsigned i=0;i<s.target_count;++i){auto t=s.targets[i];assert(t.x>=0&&t.y>=0&&t.x+t.w<=320&&t.y+t.h<=240);assert(t.h>=34);if(t.enabled)assert(hit(s,t.x+t.w/2,t.y+t.h/2).action==t.action);}
  cyd::hal_render_region(0,0,320,240,pixel,&s);ppm(std::string(argv[1])+"/page-"+std::to_string(p)+"-"+std::to_string(mode)+".ppm");
 }
 m.page=Page::keyboard;m.bt_passkey=true;m.bt_confirm=true;m.passkey="234567";m.bluetooth=Link::busy;Scene s{};build(s,m);assert(hit(s,70,160).action==Action::bt_connect);assert(hit(s,220,160).action==Action::bt_disconnect);
 m.bt_confirm=false;build(s,m);assert(hit(s,70,160).action==Action::none);
 m.page=Page::hosts;m.session_active=true;build(s,m);assert(hit(s,70,65).action==Action::none);assert(hit(s,200,210).action==Action::disconnect);
 terminal::TouchGesture gesture;assert(!gesture.sample(true,70,65,0).released);assert(!gesture.sample(false,0,0,10).released);assert(!gesture.sample(true,71,64,15).released);assert(!gesture.sample(false,0,0,40).released);assert(gesture.sample(false,0,0,61).released);assert(!gesture.sample(false,0,0,150).released);
 assert(!gesture.sample(true,70,65,200).released);assert(!gesture.sample(false,0,0,260).released); // isolated noise
 gesture.sample(true,70,65,300);for(int i=0;i<8;++i)gesture.sample(true,70,125,315+i*15);assert(gesture.sample(false,0,0,480).dragged);
 const cyd_fonts::Font* faces[]={&cyd_fonts::pixelfont_7x5_thin,&cyd_fonts::pixelfont_11x5_thin,&cyd_fonts::pixelfont_13x7_semibold};const unsigned widths[]={6,8,10},heights[]={10,14,16};
 for(unsigned i=0;i<3;++i)for(unsigned c=33;c<127;++c){cm::TerminalCell cell{};cell.glyph=c;unsigned ink=0;for(unsigned y=0;y<heights[i];++y)for(unsigned x=0;x<widths[i];++x)ink+=terminal::paint_pixel(cell,false,*faces[i],widths[i],heights[i],x,y,false)!=terminal::colour(0x081115);assert(ink>0);}
 assert(terminal::ansi(196,false)==0xf800);assert(terminal::ansi(46,false)==0x07e0);assert(terminal::ansi(21,false)==0x001f);
 cm::terminal_use_ui_scene(&s);
 for(unsigned index=0;index<3;++index){cm::terminal_set_font_index(index);assert(cm::terminal_begin(nullptr,nullptr));
  const char* data="\x1b[2J\x1b[H\x1b[97mWorkbench> \x1b[92mecho hello\r\nhello\r\n\x1b[97mWorkbench> \x1b[92mls\r\nnotes.md  projects/\r\n\x1b[38;5;196mRED \x1b[38;5;46mGREEN \x1b[38;5;51mCYAN\r\n\x1b[97mWorkbench> \x1b[92m";assert(cm::terminal_feed(reinterpret_cast<const uint8_t*>(data),std::strlen(data)));assert(cm::terminal_render(25000,true,true,"Workbench")==ESP_OK);bool red=false;for(auto c:lcd)red|=c==0xf800;assert(red);assert(transfers>0);
  ppm(std::string(argv[1])+"/terminal-"+std::to_string(index)+".ppm");
  const char* erase="\x1b[2J\x1b[H";assert(cm::terminal_feed(reinterpret_cast<const uint8_t*>(erase),std::strlen(erase)));assert(cm::terminal_render(25000,true,true,"Workbench")==ESP_OK);
  for(unsigned y=26+cm::terminal_geometry().font_height;y<240;++y)for(unsigned x=0;x<320;++x)assert(lcd[y*320+x]==terminal::colour(0x081115));assert(cm::terminal_end());
  m.page=Page::session;build(s,m);assert(hit(s,50,115).action==Action::zoom_out); // Rebuild borrowed header scene into menu.
 }
 std::puts("PASS Terminal Micro: UI hit targets/disabled actions/themes, pairing prompts, touch bounce/drag, all PixelFonts glyphs, ANSI colours, safe DMA strips and stale-row clearing");
}
