#include "terminal_ui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace terminal::ui {
namespace {
constexpr uint16_t rgb(unsigned c){return ((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31);}
struct Palette {uint16_t bg,header,surface,edge,text,muted,blue,green,orange,red,disabled;};
constexpr Palette dark{rgb(0x081115),rgb(0x101d24),rgb(0x172b32),rgb(0x29414a),rgb(0xe4eef0),rgb(0x91a9b0),rgb(0x42a5f5),rgb(0x66cf8e),rgb(0xffae42),rgb(0xef6b72),rgb(0x4d6269)};
constexpr Palette light{rgb(0xa9dcc2),rgb(0xb9e4da),rgb(0xd6efe7),rgb(0x83b6b6),rgb(0x0c3d48),rgb(0x376e73),rgb(0x147ba4),rgb(0x21795c),rgb(0xc66b16),rgb(0xb93f50),rgb(0x779b9b)};
const auto& glyph(const cyd_fonts::Font& f,unsigned char c){return f.glyphs[(c>=f.first&&c<=f.last?c:'?')-f.first];}
int width(const char* t,const cyd_fonts::Font& f,int scale=1){int w=0;for(;*t;++t)w+=(glyph(f,*t).advance+1)*scale;return w?w-scale:0;}
void rect(Scene& s,int x,int y,int w,int h,uint16_t c){if(s.count<scene_capacity)s.elements[s.count++]={Kind::rectangle,x,y,w,h,c};}
void text(Scene& s,const char* value,int x,int y,uint16_t c,int max_w=304,const cyd_fonts::Font& f=cyd_fonts::pixelfont_11x5_thin,int scale=1){
 if(s.count>=scene_capacity)return;
 auto& e=s.elements[s.count++];e={Kind::text,x,y,std::max(0,std::min(max_w,320-x)),(f.ascent+3)*scale,c,&f,scale};
 std::snprintf(e.text,sizeof(e.text),"%s",value?value:"");size_t n=std::strlen(e.text);
 while(n&&width(e.text,f,scale)>e.w)e.text[--n]=0;
}
void centre(Scene& s,const char* t,int x,int y,int w,uint16_t c,const cyd_fonts::Font& f=cyd_fonts::pixelfont_11x5_thin,int scale=1){text(s,t,x+std::max(0,(w-width(t,f,scale))/2),y,c,w,f,scale);}
void target(Scene& s,int x,int y,int w,int h,Action a,bool enable=true,unsigned index=0){if(s.target_count<12)s.targets[s.target_count++]={x,y,w,h,a,index,enable};}
void button(Scene& s,const Palette& p,int x,int y,int w,int h,const char* label,Action a,bool enabled=true,uint16_t accent=0,unsigned index=0){
 rect(s,x,y,w,h,enabled?(accent?accent:p.edge):p.surface);
 centre(s,label,x,y+(h-14)/2,w,enabled?p.text:p.disabled);target(s,x,y,w,h,a,enabled,index);
}
uint16_t status(const Palette& p,Link l){return l==Link::ready?p.green:l==Link::busy?p.orange:l==Link::error?p.red:p.red;}
void icon(Scene& s,int x,int y,int w,int h,uint16_t c){if(s.count<scene_capacity)s.elements[s.count++]={Kind::terminal,x,y,w,h,c};}
void back(Scene& s,const Palette& p){button(s,p,8,194,304,40,"Back",Action::home);}
void home_card(Scene& s,const Palette& p,int y,const char* label,const char* state,Link l,Action a){
 rect(s,8,y,304,42,p.surface);rect(s,8,y,4,42,status(p,l));
 text(s,label,22,y+7,p.text,144);text(s,state,170,y+7,status(p,l),130);
 text(s,"Tap to manage",22,y+26,p.muted,140,cyd_fonts::pixelfont_7x5_thin);text(s,">",289,y+15,p.blue,15);
 target(s,8,y,304,42,a);
}
}
bool terminal_icon_ink(int x,int y,int w,int h){
 // Normalized 30x24: thick rounded window, three title dots, chevron and cursor.
 if(x<0||y<0||x>=w||y>=h)return false;
 const int u=x*30/w,v=y*24/h;
 const bool border=((v==1||v==2||v==21||v==22)&&u>=3&&u<=26)||
                   ((u==1||u==2||u==27||u==28)&&v>=3&&v<=20);
 const bool title=v==6&&u>=3&&u<=26;
 const bool dots=v==4&&(u==5||u==8||u==11);
 const bool prompt=v>=10&&v<=18&&std::abs(u-(7+4-std::abs(v-14)))<=1;
 const bool cursor=v>=17&&v<=18&&u>=16&&u<=23;
 return border||title||dots||prompt||cursor;
}
Target hit(const Scene& s,int x,int y){for(unsigned i=0;i<s.target_count;++i){const auto& t=s.targets[i];if(t.enabled&&x>=t.x&&y>=t.y&&x<t.x+t.w&&y<t.y+t.h)return t;}return {};}
void build(Scene& s,const Model& m){
 s.count=0;s.target_count=0;const auto p=m.light?light:dark;s.background=p.bg;
 rect(s,0,0,320,42,p.header);icon(s,8,7,30,26,p.blue);
 text(s,"Terminal Micro",46,6,p.text,178,cyd_fonts::pixelfont_13x7_semibold);
 text(s,m.version,46,28,p.muted,178,cyd_fonts::pixelfont_7x5_thin);
 button(s,p,238,4,74,34,m.light?"Dark":"Light",Action::theme);
 if(m.page==Page::home){
  home_card(s,p,46,"Wi-Fi",m.wifi_status,m.wifi,Action::wifi);
  home_card(s,p,92,"Keyboard",m.bt_status,m.bluetooth,Action::keyboard);
  home_card(s,p,138,m.host_label,m.ssh_status,m.ssh,Action::hosts);
  char info[64];std::snprintf(info,sizeof(info),"%s  Free RAM: %uK",m.config_ok?"":"SD config error",m.free_ram_kb);
  text(s,info,12,183,m.config_ok?p.muted:p.red,300,cyd_fonts::pixelfont_7x5_thin);
  button(s,p,8,198,190,36,m.session_active?"Resume terminal":"Open terminal",m.session_active?Action::resume:Action::connect,m.session_active||(m.config_ok&&m.wifi==Link::ready&&m.host_count),p.blue);
  button(s,p,206,198,106,36,"Console",Action::console,true,p.edge);
 }else if(m.page==Page::keyboard){
  text(s,"Winmaxle B1",12,49,p.text,190,cyd_fonts::pixelfont_13x7_semibold);
  text(s,m.bt_status,12,73,status(p,m.bluetooth),290);
  char info[64];std::snprintf(info,sizeof(info),"%s  Keys: %u",m.bt_saved?"Saved keyboard":"No saved keyboard",m.keys);
  text(s,m.bt_passkey?(m.bt_confirm?"Check code, then tap Confirm":"Type this code on B1, then Enter"):info,12,92,p.muted,296,cyd_fonts::pixelfont_7x5_thin);
  if(m.bt_passkey){centre(s,m.passkey,8,103,304,p.orange,cyd_fonts::pixelfont_13x7_semibold,2);}
  else text(s,m.message[0]?m.message:"Wake B1 with a key; tap Connect",12,113,p.muted,296,cyd_fonts::pixelfont_7x5_thin);
  const bool busy=m.bluetooth==Link::busy;
  const char* primary=m.bt_confirm?"Confirm":m.bt_found?"Pair":m.bt_saved?"Connect":"Scan B1";
  button(s,p,8,144,148,42,primary,Action::bt_connect,m.bt_confirm||(!busy&&m.bluetooth!=Link::ready),p.blue);
  button(s,p,164,144,148,42,m.bt_confirm?"Reject":m.bluetooth==Link::ready?"Disconnect":m.forget_armed?"Forget?":"Forget",m.bt_confirm||m.bluetooth==Link::ready?Action::bt_disconnect:Action::bt_forget,m.bt_confirm||m.bluetooth==Link::ready||(!busy&&m.bt_saved),m.forget_armed?p.red:0);
  back(s,p);
 }else if(m.page==Page::wifi){
  text(s,"Wi-Fi",12,50,p.text,294,cyd_fonts::pixelfont_13x7_semibold);
  text(s,m.wifi_status,12,76,status(p,m.wifi));text(s,m.wifi_ssid,12,100,p.text,296);
  text(s,m.wifi_ip,12,121,p.muted,296,cyd_fonts::pixelfont_7x5_thin);
  button(s,p,8,144,148,42,"Retry",Action::wifi_retry,m.config_ok&&!m.session_active,p.blue);
  button(s,p,164,144,148,42,"Next network",Action::wifi_next,m.config_ok&&!m.session_active);
  back(s,p);
 }else if(m.page==Page::hosts){
  const unsigned first=m.selected_host/2*2;
  for(unsigned i=0;i<2&&first+i<m.host_count;++i){unsigned index=first+i;const int x=8+i*156;
   button(s,p,x,48,148,44,m.hosts[index].label,Action::host_select,!m.session_active,index==m.selected_host?p.blue:0,index);
  }
  rect(s,8,98,304,86,p.surface);text(s,m.host_label,16,105,p.text,288);
  char details[96];std::snprintf(details,sizeof(details),"%s:%u",m.host_address,m.host_port);text(s,details,16,124,p.muted,288,cyd_fonts::pixelfont_7x5_thin);
  std::snprintf(details,sizeof(details),"User: %s  / Key authentication",m.host_user);text(s,details,16,141,p.muted,288,cyd_fonts::pixelfont_7x5_thin);
  std::snprintf(details,sizeof(details),"%s%s",m.ssh_status,m.session_active?" / Disconnect to change":"");text(s,details,16,162,status(p,m.ssh),288,cyd_fonts::pixelfont_7x5_thin);
  button(s,p,8,194,70,40,"Prev",Action::host_prev,m.host_count>1&&!m.session_active);
  button(s,p,84,194,70,40,"Next",Action::host_next,m.host_count>1&&!m.session_active);
  button(s,p,160,194,74,40,m.session_active?"Stop":"Back",m.session_active?Action::disconnect:Action::home);
  button(s,p,240,194,72,40,"Open",Action::connect,m.config_ok&&m.wifi==Link::ready&&m.host_count&&!m.session_active,p.blue);
 }else{
  text(s,m.host_label,12,51,p.red,296,cyd_fonts::pixelfont_13x7_semibold);
  text(s,"Terminal controls",12,74,p.muted,296);
  button(s,p,8,99,148,40,"Font -",Action::zoom_out,m.font_index>0);
  button(s,p,164,99,148,40,"Font +",Action::zoom_in,m.font_index<2);
  button(s,p,8,147,148,40,"Live output",Action::live);
  button(s,p,164,147,148,40,"Disconnect",Action::disconnect,true,p.red);
  button(s,p,8,194,148,40,"Resume",Action::resume,true,p.blue);
  button(s,p,164,194,148,40,"Console",Action::console);
 }
}
void build_header(Scene& s,bool is_light,bool wifi,bool bt,const char* host,unsigned history){
 s.count=0;s.target_count=0;auto p=is_light?light:dark;s.background=p.header;
 icon(s,4,2,25,22,p.blue);text(s,host,36,6,p.red,142);
 char info[32];std::snprintf(info,sizeof(info),"H:%u",history);text(s,info,180,8,p.muted,46,cyd_fonts::pixelfont_7x5_thin);
 rect(s,237,9,6,6,wifi?p.green:p.red);rect(s,247,9,6,6,bt?p.green:p.red);
 text(s,"MENU",264,7,p.text,52,cyd_fonts::pixelfont_7x5_thin);
}
uint16_t pixel(int x,int y,void* user){
 const auto& s=*static_cast<Scene*>(user);uint16_t c=s.background;
 for(unsigned i=0;i<s.count;++i){const auto& e=s.elements[i];if(x<e.x||y<e.y||x>=e.x+e.w||y>=e.y+e.h)continue;
  if(e.kind==Kind::rectangle)c=e.colour;
  else if(e.kind==Kind::terminal){if(terminal_icon_ink(x-e.x,y-e.y,e.w,e.h))c=e.colour;}
  else if(e.kind==Kind::text&&e.font){const auto& f=*e.font;int cursor=e.x;
   for(const unsigned char* t=reinterpret_cast<const unsigned char*>(e.text);*t;++t){const auto& g=glyph(f,*t);const int gx=(x-cursor)/e.scale-g.x,gy=(y-e.y)/e.scale-f.ascent-g.y;
    if(x>=cursor&&gx>=0&&gy>=0&&gx<g.width&&gy<g.height){unsigned bit=gy*g.width+gx;if(f.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))c=e.colour;}
    cursor+=(g.advance+1)*e.scale;if(cursor>x)break;
   }
  }
 }return c;
}
}
