#pragma once
#include "terminal_parser.h"
#include "pixel_fonts.h"
#include <algorithm>
namespace terminal {
constexpr uint16_t colour(unsigned c){return ((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31);}
inline uint16_t ansi(uint8_t c,bool light){
 constexpr unsigned dark[]={0x000000,0xef5350,0x66bb6a,0xffca28,0x42a5f5,0xab47bc,0x26c6da,0xeeeeee,0x757575,0xff7043,0x81c784,0xffd54f,0x64b5f6,0xba68c8,0x4dd0e1,0xffffff};
 constexpr unsigned bright[]={0x000000,0xb93f50,0x21795c,0xc66b16,0x147ba4,0x892a92,0x1c6e83,0x779b9b,0x52666a,0xd22838,0x165b45,0xdb6200,0x0e5d7d,0x731f7c,0x105566,0x0c3d48};
 if(c<16)return colour((light?bright:dark)[c]);
 if(c>=232){unsigned v=8+(c-232)*10;return colour(v<<16|v<<8|v);}
 constexpr unsigned level[]={0,95,135,175,215,255};unsigned n=c-16;
 return colour(level[n/36]<<16|level[n/6%6]<<8|level[n%6]);
}
inline uint16_t paint_pixel(const cm::TerminalCell& cell,bool cursor,const cyd_fonts::Font& font,unsigned cw,unsigned ch,unsigned x,unsigned y,bool light){
 using cm::CellFlag;
 auto fg=cell.flags&CellFlag::default_fg?colour(light?0x0c3d48:0xe4eef0):ansi(cell.fg,light);
 auto bg=cell.flags&CellFlag::default_bg?colour(light?0xa9dcc2:0x081115):ansi(cell.bg,light);
 if(cell.flags&CellFlag::reverse)std::swap(fg,bg);
 if(cursor)std::swap(fg,bg);
 const auto& g=font.glyphs[(cell.glyph>=font.first&&cell.glyph<=font.last?cell.glyph:'?')-font.first];
 // Signed arithmetic avoids underflow for glyphs wider than their advance.
 const int padx=std::max(0,(int(cw)-int(g.width))/2)-g.x;
 const int gx=int(x)-padx-g.x,gy=int(y)-font.ascent-1-g.y;
 bool ink=false;
 if(!(cell.flags&CellFlag::conceal)&&gx>=0&&gy>=0&&gx<g.width&&gy<g.height){unsigned bit=gy*g.width+gx;ink=font.bitmap[g.offset+bit/8]&(0x80>>(bit%8));}
 if(!(cell.flags&CellFlag::conceal)&&(cell.flags&CellFlag::underline)&&y+2==ch)ink=true;
 return ink?fg:bg;
}
}
