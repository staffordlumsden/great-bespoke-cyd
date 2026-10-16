#include "quicknote_ui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace quicknote::ui {
namespace {
constexpr uint16_t rgb(unsigned colour){return ((colour>>19)&31)<<11|((colour>>10)&63)<<5|((colour>>3)&31);}
struct Palette {uint16_t bg,header,surface,edge,text,muted,blue,green,orange,red,disabled;};
constexpr Palette dark{rgb(0x081115),rgb(0x101d24),rgb(0x172b32),rgb(0x29414a),rgb(0xe4eef0),rgb(0x91a9b0),rgb(0x42a5f5),rgb(0x66cf8e),rgb(0xffae42),rgb(0xef6b72),rgb(0x4d6269)};
constexpr Palette light{rgb(0xa9dcc2),rgb(0xb9e4da),rgb(0xd6efe7),rgb(0x83b6b6),rgb(0x0c3d48),rgb(0x376e73),rgb(0x147ba4),rgb(0x21795c),rgb(0xc66b16),rgb(0xb93f50),rgb(0x779b9b)};
const cyd_fonts::Font& face(int index){return index<=0?cyd_fonts::pixelfont_7x5_thin:index==1?cyd_fonts::pixelfont_11x5_thin:cyd_fonts::pixelfont_13x7_semibold;}
int line_height(int index){return index<=0?9:index==1?13:15;}
const cyd_fonts::Glyph& glyph(const cyd_fonts::Font& font,unsigned char c){return font.glyphs[(c>=font.first&&c<=font.last?c:'?')-font.first];}
int width(const char* value,const cyd_fonts::Font& font,int scale=1){int result=0;for(;*value;++value)result+=(glyph(font,*value).advance+1)*scale;return result?result-scale:0;}
void rect(Scene& s,int x,int y,int w,int h,uint16_t colour){if(s.count<56)s.elements[s.count++]={Kind::rectangle,x,y,w,h,colour};}
void text(Scene& s,const char* value,int x,int y,const cyd_fonts::Font& font,uint16_t colour,int max_width=320){
    if(s.count>=56)return;
    auto& e=s.elements[s.count++];e={Kind::text,x,y,std::max(0,std::min(max_width,320-x)),font.ascent+3,colour,&font,1};
    std::snprintf(e.text,sizeof(e.text),"%s",value?value:"");size_t n=std::strlen(e.text);bool clipped=width(e.text,font)>e.w;
    while(n&&width(e.text,font)>e.w)e.text[--n]=0;
    if(clipped&&n>=3)e.text[n-1]=e.text[n-2]=e.text[n-3]='.';
}
void centre(Scene& s,const char* value,int cx,int y,const cyd_fonts::Font& font,uint16_t colour){text(s,value,cx-width(value,font)/2,y,font,colour);}
void button(Scene& s,int x,int w,const char* label,bool enabled,const Palette& p,uint16_t active=0){
    rect(s,x,207,w,31,enabled?(active?active:p.edge):p.surface);centre(s,label,x+w/2,217,cyd_fonts::pixelfont_7x5_thin,enabled?p.text:p.disabled);
}
struct Line {size_t start=0,end=0;};
struct LineReader {size_t position=0;bool pending=true;};
bool next_line(const char* content,size_t size,const cyd_fonts::Font& font,LineReader& reader,Line& line) {
    if(!reader.pending)return false;
    line.start=reader.position;int pixels=0;
    while(reader.position<size&&content[reader.position]!='\n') {
        const int next=glyph(font,static_cast<unsigned char>(content[reader.position])).advance+1;
        if(reader.position>line.start&&pixels+next>296)break;
        pixels+=next;++reader.position;
    }
    line.end=reader.position;
    if(reader.position<size&&content[reader.position]=='\n')++reader.position;
    else if(reader.position>=size)reader.pending=false;
    return true;
}
bool contains_cursor(const Line& line,const char* content,size_t size,size_t cursor) {
    if(cursor<line.start||cursor>line.end)return false;
    if(cursor<line.end||line.end>=size)return true;
    return content[line.end]=='\n';
}
unsigned line_count(const char* content,size_t size,const cyd_fonts::Font& font,unsigned* cursor_line,size_t cursor) {
    if(!content){content="";size=0;}
    LineReader reader{};Line line{};unsigned count=0;
    while(next_line(content,size,font,reader,line)) {
        if(cursor_line&&contains_cursor(line,content,size,cursor))*cursor_line=count;
        ++count;
    }
    return count;
}
void content(Scene& s,const Model& m,const Palette& p) {
    const auto& font=face(m.font_index);unsigned cursor_line=0;
    const unsigned count=line_count(m.content,m.content_size,font,m.history?nullptr:&cursor_line,m.cursor);
    const unsigned visible=visible_rows(m.font_index);unsigned first=0;
    if(m.history)first=std::min<unsigned>(std::max(0,m.history_scroll),count>visible?count-visible:0);
    else first=cursor_line>=visible?cursor_line-visible+1:0;
    const int step=line_height(m.font_index);
    LineReader reader{};Line line{};unsigned index=0,row=0;
    while(row<visible&&next_line(m.content?m.content:"",m.content?m.content_size:0,font,reader,line)) {
        if(index++<first)continue;
        char value[64]{};const size_t n=std::min(sizeof(value)-1,line.end-line.start);
        if(n)std::memcpy(value,m.content+line.start,n);
        text(s,value,12,57+row*step,font,p.text,296);
        if(!m.history&&contains_cursor(line,m.content,m.content_size,m.cursor)) {
            char before[64]{};const size_t prior=std::min(sizeof(before)-1,m.cursor-line.start);if(prior)std::memcpy(before,m.content+line.start,prior);
            rect(s,12+width(before,font),56+row*step,2,font.ascent+2,p.orange);
        }
        ++row;
    }
    if(m.history&&count>visible){char page[24];std::snprintf(page,sizeof(page),"Lines %u-%u/%u",first+1,std::min(first+visible,count),count);text(s,page,218,190,cyd_fonts::pixelfont_7x5_thin,p.muted,94);}
}
}

unsigned visible_rows(int index){return index<=0?14:index==1?10:8;}
void build(Scene& s,const Model& m) {
    s.count=0;const auto p=m.light?light:dark;s.background=p.bg;
    rect(s,0,0,320,50,p.header);text(s,"Quicknote",8,5,cyd_fonts::pixelfont_13x7_semibold,p.orange,115);text(s,"Micro",126,7,cyd_fonts::pixelfont_11x5_thin,p.muted,48);
    if(s.count<56)s.elements[s.count++]={Kind::clock,242,5,16,16,m.clock_valid?p.green:p.red};
    text(s,m.time,262,7,cyd_fonts::pixelfont_11x5_thin,m.clock_valid?p.text:p.red,50);
    text(s,m.date,8,31,cyd_fonts::pixelfont_7x5_thin,p.muted,146);
    rect(s,158,25,86,23,p.surface);if(s.count<56)s.elements[s.count++]={Kind::bluetooth,163,28,14,16,m.bluetooth_connected?p.blue:p.muted};text(s,m.bluetooth,181,32,cyd_fonts::pixelfont_7x5_thin,m.bluetooth_connected?p.blue:p.muted,58);
    rect(s,250,25,62,23,p.surface);centre(s,m.light?"LIGHT":"DARK",281,32,cyd_fonts::pixelfont_7x5_thin,p.text);
    rect(s,8,53,304,133,p.surface);content(s,m,p);
    rect(s,8,189,304,15,p.header);text(s,m.status,12,193,cyd_fonts::pixelfont_7x5_thin,m.clock_valid?p.muted:p.red,296);
    constexpr int x[]={4,56,108,160,212,264};
    if(m.history){button(s,x[0],48,"EDIT",true,p,p.blue);button(s,x[1],48,"PREV",m.clock_valid,p);button(s,x[2],48,"NEXT",m.clock_valid&&m.day_offset<0,p);}
    else{button(s,x[0],48,"TODAY",m.clock_valid,p,p.blue);button(s,x[1],48,"SAVE",m.save_enabled,p,p.green);button(s,x[2],48,m.button_clear_armed?"SURE?":"CLEAR",m.content_size>0,p,m.button_clear_armed?p.red:0);}
    button(s,x[3],48,"A-",m.font_index>0,p);button(s,x[4],48,"A+",m.font_index<2,p);button(s,x[5],52,"CON",true,p,p.orange);
}
uint16_t pixel(int x,int y,void* context) {
    const auto& s=*static_cast<Scene*>(context);uint16_t colour=s.background;
    for(unsigned i=0;i<s.count;++i){const auto& e=s.elements[i];if(x<e.x||y<e.y||x>=e.x+e.w||y>=e.y+e.h)continue;
        if(e.kind==Kind::rectangle)colour=e.colour;
        else if(e.kind==Kind::clock){const int dx=x-e.x-8,dy=y-e.y-8,d=dx*dx+dy*dy;if((d>=36&&d<=64)||(dx>=0&&dx<=1&&dy>=-4&&dy<=1)||(dx>=0&&dx<=4&&dy>=0&&dy<=3&&(dy==dx||dy==dx-1)))colour=e.colour;}
        else if(e.kind==Kind::bluetooth){const int dx=x-e.x,dy=y-e.y;if((dx>=6&&dx<=7)||(dy==dx-2&&dx>=7&&dx<=12)||(dy==14-dx&&dx>=7&&dx<=12)||(dy==dx+2&&dx>=1&&dx<=7)||(dy==12-dx&&dx>=1&&dx<=7))colour=e.colour;}
        else {const auto& font=*e.font;int cursor=e.x,baseline=e.y+font.ascent;
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>(e.text);*c;++c){const auto& g=glyph(font,*c);const int gx=x-cursor-g.x,gy=y-baseline-g.y;
                if(gx>=0&&gy>=0&&gx<g.width&&gy<g.height){const unsigned bit=gy*g.width+gx;if(font.bitmap[g.offset+bit/8]&(0x80>>(bit%8)))colour=e.colour;}
                cursor+=g.advance+1;if(cursor>x)break;}
        }
    }return colour;
}
}
