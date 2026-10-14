#pragma once
#include "pixel_fonts.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fsapp::ui {
constexpr uint16_t rgb(unsigned colour){return ((colour>>19)&31)<<11|((colour>>10)&63)<<5|((colour>>3)&31);}
struct Palette {uint16_t bg,surface,secondary,text,muted,blue,green,disabled;};
constexpr Palette dark{rgb(0x0b1519),rgb(0x172b32),rgb(0x23505d),rgb(0xd5e7eb),rgb(0x8dadb5),rgb(0x41a5d4),rgb(0x70d28b),rgb(0x455961)};
constexpr Palette light{rgb(0xa9dcc2),rgb(0xc3e8dd),rgb(0xb5d9ed),rgb(0x0c3d48),rgb(0x376e73),rgb(0x147ba4),rgb(0x21795c),rgb(0x83b6b6)};
enum class Kind {rect,text,sd,sun,clock};
struct Element {Kind kind;int x=0,y=0,w=0,h=0;uint16_t colour=0;const cyd_fonts::Font* font=nullptr;int scale=1;char text[80]{};};
struct Scene {Element elements[32]{};unsigned count=0;Palette colours=dark;};
inline int width(const char* text,const cyd_fonts::Font& face,int scale=1){int n=0;for(;*text;++text){unsigned c=static_cast<unsigned char>(*text);const auto& g=face.glyphs[(c>=32&&c<=126?c:'?')-32];n+=(g.advance+1)*scale;}return n?n-scale:0;}
inline void rect(Scene& s,int x,int y,int w,int h,uint16_t colour){if(s.count<32)s.elements[s.count++]={Kind::rect,x,y,w,h,colour};}
inline void text(Scene& s,const char* value,int x,int y,const cyd_fonts::Font& face,uint16_t colour,int scale=1,int max_width=304){
 if(s.count==32)return;
 max_width=std::max(0,std::min(max_width,320-x));
 auto& e=s.elements[s.count++];e={Kind::text,x,y,max_width,face.ascent*scale+4,colour,&face,scale};std::snprintf(e.text,sizeof(e.text),"%s",value);
 unsigned n=std::strlen(e.text);bool clipped=width(e.text,face,scale)>max_width;while(n&&width(e.text,face,scale)>max_width)e.text[--n]=0;if(clipped&&n>=3){e.text[n-1]=e.text[n-2]=e.text[n-3]='.';}
}
inline void centre(Scene& s,const char* value,int cx,int y,const cyd_fonts::Font& face,uint16_t colour){text(s,value,cx-width(value,face)/2,y,face,colour);}
inline void button(Scene& s,int x,int y,int w,int h,const char* title,bool enabled=true){rect(s,x,y,w,h,enabled?s.colours.secondary:s.colours.surface);centre(s,title,x+w/2,y+(h-11)/2,cyd_fonts::pixelfont_11x5_thin,enabled?s.colours.text:s.colours.disabled);}
struct Model {bool light=false,networks=false,setup=false,connected=false,busy=false,stopping=false,clock_sync=false;const char* version="",*ip="",*ssid="",*setup_ssid="",*code="",*name="",*phase="Ready",*message="";uint64_t done=0,total=0,elapsed_ms=0,card_free=0;};
inline void build(Scene& s,const Model& m){
 s.count=0;s.colours=m.light?light:dark;auto c=s.colours;
 if(s.count<32)s.elements[s.count++]={m.clock_sync?Kind::clock:Kind::sd,10,8,20,20,c.green};
 text(s,m.clock_sync?"Clock Sync":m.networks?"Networks":"File Server",37,5,cyd_fonts::pixelfont_13x7_semibold,c.text,2,200);
 text(s,m.version,12,37,cyd_fonts::pixelfont_7x5_thin,c.muted);
 rect(s,244,5,68,43,c.surface);if(s.count<32)s.elements[s.count++]={Kind::sun,267,8,20,20,c.blue};centre(s,m.light?"LIGHT":"DARK",278,31,cyd_fonts::pixelfont_7x5_thin,c.text);
 if(m.clock_sync){rect(s,8,56,304,144,c.surface);text(s,m.phase,16,73,cyd_fonts::pixelfont_13x7_semibold,c.text);text(s,m.message,16,103,cyd_fonts::pixelfont_7x5_thin,c.muted);rect(s,16,127,288,10,c.secondary);rect(s,16+int((m.elapsed_ms/20)%216),127,72,10,c.green);text(s,"Returns to Console when finished",16,153,cyd_fonts::pixelfont_7x5_thin,c.muted);text(s,"Tap Stop to return without waiting",16,174,cyd_fonts::pixelfont_7x5_thin,c.muted);button(s,8,211,304,27,"Stop");return;}
 if(m.networks){rect(s,8,56,304,144,c.surface);
  text(s,m.setup?"Setup hotspot":m.connected?"Wi-Fi connected":"Join a network",16,63,cyd_fonts::pixelfont_13x7_semibold,c.text);
  text(s,m.setup?m.setup_ssid:m.ssid,16,85,cyd_fonts::pixelfont_11x5_thin,c.blue);
  if(m.setup){text(s,"Hotspot password / access code",16,109,cyd_fonts::pixelfont_7x5_thin,c.muted);text(s,m.code,16,121,cyd_fonts::pixelfont_7x5_thin,c.text,2);text(s,"192.168.4.1:8080",16,147,cyd_fonts::pixelfont_13x7_semibold,c.blue);text(s,"Choose Networks in the browser",16,176,cyd_fonts::pixelfont_7x5_thin,c.muted);}
  else{text(s,"Open Networks in the browser to join",16,110,cyd_fonts::pixelfont_7x5_thin,c.muted);text(s,"another 2.4 GHz Wi-Fi network",16,126,cyd_fonts::pixelfont_7x5_thin,c.muted);char url[40];std::snprintf(url,sizeof(url),"%s:8080",m.ip);text(s,m.connected?url:"Or enable the setup hotspot below",16,151,cyd_fonts::pixelfont_11x5_thin,c.blue);}
  button(s,8,211,148,27,m.setup?"Hotspot on":"Setup hotspot",!m.setup&&!m.busy);button(s,164,211,148,27,"Back");return;
 }
 rect(s,8,56,304,58,c.surface);text(s,m.setup?"SETUP HOTSPOT":m.connected?"WI-FI CONNECTED":"WI-FI CONNECTING",16,62,cyd_fonts::pixelfont_7x5_thin,c.muted);rect(s,293,62,7,7,m.connected?c.green:rgb(0xe5b750));
 char endpoint[40];std::snprintf(endpoint,sizeof(endpoint),"%s:8080",m.setup?"192.168.4.1":m.ip);
 text(s,m.connected||m.setup?endpoint:"Connecting...",16,77,cyd_fonts::pixelfont_13x7_semibold,c.blue);
 text(s,"CODE",16,102,cyd_fonts::pixelfont_7x5_thin,c.muted);
 char code[16];std::snprintf(code,sizeof(code),"%.4s-%.4s",m.code,m.code+std::min<size_t>(4,std::strlen(m.code)));text(s,code,54,96,cyd_fonts::pixelfont_7x5_thin,c.text,2,250);
 rect(s,8,120,304,74,c.surface);
 text(s,m.stopping?"Stopping...":m.phase,16,125,cyd_fonts::pixelfont_13x7_semibold,c.text);
 text(s,m.name[0]?m.name:m.message[0]?m.message:"Open this address in a browser",16,146,cyd_fonts::pixelfont_7x5_thin,c.muted);
 rect(s,16,161,288,10,c.secondary);unsigned percent=m.total?std::min<uint64_t>(100,m.done*100/m.total):m.busy?0:100;
 if(m.busy||m.done||m.total)rect(s,16,161,288*percent/100,10,c.green);
 char detail[80];if(m.busy||m.done||m.total)std::snprintf(detail,sizeof(detail),"%u%%  %llu.%llu/%llu.%llu MiB  %llu KiB/s",percent,static_cast<unsigned long long>(m.done/1048576),static_cast<unsigned long long>(m.done%1048576*10/1048576),static_cast<unsigned long long>(m.total/1048576),static_cast<unsigned long long>(m.total%1048576*10/1048576),static_cast<unsigned long long>(m.elapsed_ms?m.done*1000/m.elapsed_ms/1024:0));else std::snprintf(detail,sizeof(detail),"Uploads / downloads from the SD card");
 text(s,detail,16,180,cyd_fonts::pixelfont_7x5_thin,c.muted);
 char free[40];std::snprintf(free,sizeof(free),"SD  %llu.%llu GiB free",static_cast<unsigned long long>(m.card_free/1073741824),static_cast<unsigned long long>(m.card_free%1073741824*10/1073741824));text(s,free,12,197,cyd_fonts::pixelfont_7x5_thin,c.muted);
 button(s,8,211,96,27,"Retry",!m.busy);button(s,112,211,96,27,"Networks",!m.busy);button(s,216,211,96,27,m.busy?"Stop":"Console");
}
inline uint16_t pixel(int x,int y,void* context){const auto& s=*static_cast<Scene*>(context);uint16_t colour=s.colours.bg;
 for(unsigned i=0;i<s.count;++i){const auto& e=s.elements[i];if(x<e.x||y<e.y||x>=e.x+e.w||y>=e.y+e.h)continue;
  if(e.kind==Kind::rect)colour=e.colour;
  else if(e.kind==Kind::sd){int dx=x-e.x,dy=y-e.y;if((dy>=1&&dy<=2&&dx>=6&&dx<=18)||(dx>=17&&dy>=1&&dy<=19)||(dy>=18&&dx>=3&&dx<=18)||(dx>=3&&dx<=4&&dy>=5&&dy<=19)||(dy>=1&&dy<=5&&dx>=7-dy&&dx<=8-dy)||(dy>=4&&dy<=8&&dx>=7&&dx<=17&&(dx-7)%3<2))colour=e.colour;}
  else if(e.kind==Kind::sun){int dx=x-e.x-10,dy=y-e.y-10,d=dx*dx+dy*dy;if((d>=16&&d<=36)||((dx>=-1&&dx<=1)&&(dy<=-8||dy>=8))||((dy>=-1&&dy<=1)&&(dx<=-8||dx>=8)))colour=e.colour;}
  else if(e.kind==Kind::clock){int dx=x-e.x-10,dy=y-e.y-10,d=dx*dx+dy*dy;if((d>=64&&d<=81)||(dx>=0&&dx<=1&&dy>=-5&&dy<=1)||(dx>=0&&dx<=4&&dy>=0&&dy<=4&&(dy==dx||dy==dx-1)))colour=e.colour;}
  else if(e.kind==Kind::text){const auto& f=*e.font;int cursor=e.x,baseline=e.y+f.ascent*e.scale;
   for(const unsigned char* p=reinterpret_cast<const unsigned char*>(e.text);*p;++p){const auto& g=f.glyphs[(*p>=32&&*p<=126?*p:'?')-32];int gx=(x-cursor-g.x*e.scale),gy=(y-baseline-g.y*e.scale);
    if(gx>=0&&gy>=0&&gx<g.width*e.scale&&gy<g.height*e.scale){unsigned bit=(gy/e.scale)*g.width+gx/e.scale;if(f.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))colour=e.colour;}
    cursor+=(g.advance+1)*e.scale;if(cursor>x)break;
   }
  }
 }return colour;
}
}
