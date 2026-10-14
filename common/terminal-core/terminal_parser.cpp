#include "terminal_parser.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cm {
namespace {
uint16_t rgb(unsigned r,unsigned g,unsigned b){return static_cast<uint16_t>(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
uint8_t cube(int r,int g,int b){
 constexpr int levels[]={0,95,135,175,215,255};
 const auto nearest=[&](int value,bool green){unsigned best=0;int distance=256;for(unsigned i=0;i<6;++i){const int actual=green?((levels[i]>>2)*255/63):((levels[i]>>3)*255/31);const int delta=value>actual?value-actual:actual-value;if(delta<distance){best=i;distance=delta;}}return best;};
 uint8_t best=static_cast<uint8_t>(16+36*nearest(r,false)+6*nearest(g,true)+nearest(b,false));
 const auto error=[&](uint8_t index){const uint16_t c=terminal_colour(index);const int red=((c>>11)&31)*255/31,green=((c>>5)&63)*255/63,blue=(c&31)*255/31;const int dr=r-red,dg=g-green,db=b-blue;return dr*dr+dg*dg+db*db;};
 int distance=error(best);
 for(unsigned i=0;i<40;++i){const auto index=static_cast<uint8_t>(i<16?i:232+i-16);const int candidate=error(index);if(candidate<distance){best=index;distance=candidate;}}
 return best;
}
}
uint16_t terminal_colour(uint8_t i){
    constexpr uint16_t basic[]={0x0000,0xa800,0x0540,0xad40,0x0015,0xa815,0x0555,0xad55,
                              0x52aa,0xfaaa,0x57ea,0xffea,0x52bf,0xfabf,0x57ff,0xffff};
    if(i<16)return basic[i];
    if(i>=232){const unsigned grey=8+10*(i-232);return rgb(grey,grey,grey);}
    unsigned n=i-16;const unsigned r=n/36,g=(n/6)%6,b=n%6;
    const auto level=[](unsigned v){return v?55+40*v:0;};return rgb(level(r),level(g),level(b));
}
TerminalParser::TerminalParser(TerminalStorage& s,Reply r,void* c,unsigned columns,unsigned rows):columns_(std::clamp(columns,1u,terminal_columns)),rows_(std::clamp(rows,1u,terminal_rows)),cells_(columns_*rows_),storage_(s),reply_(r),context_(c){reset();}
bool TerminalParser::resize(unsigned columns,unsigned rows,TerminalCell* scratch){
    if(!scratch || !columns || !rows || columns>terminal_columns || rows>terminal_rows)return false;
    const unsigned old_columns=columns_,old_rows=rows_;
    const unsigned main_start=(alternate_?main_y_:y_)>=rows?(alternate_?main_y_:y_)-rows+1:0;
    const unsigned alternate_start=y_>=rows?y_-rows+1:0;
    for(unsigned layer=0;layer<2;++layer){
        TerminalCell* target=layer?storage_.alternate:storage_.main;
        std::copy_n(target,old_columns*old_rows,scratch);
        std::fill_n(target,terminal_cells,TerminalCell{});
        const unsigned start=layer?alternate_start:main_start;
        for(unsigned row=0;row<rows && row+start<old_rows;++row)
            std::copy_n(scratch+(row+start)*old_columns,std::min(old_columns,columns),target+row*columns);
    }
    const unsigned start=alternate_?alternate_start:main_start;
    x_=std::min(x_,columns-1);y_=std::min(y_-std::min(y_,start),rows-1);
    main_x_=std::min(main_x_,columns-1);main_y_=std::min(main_y_-std::min(main_y_,main_start),rows-1);
    saved_x_=std::min(saved_x_,columns-1);saved_y_=std::min(saved_y_-std::min(saved_y_,start),rows-1);
    columns_=columns;rows_=rows;cells_=columns*rows;top_=0;bottom_=rows-1;origin_=false;pending_wrap_=false;scan_=0;
    history_count_=history_head_=view_offset_=0;std::fill_n(storage_.history,terminal_columns*terminal_history_rows,TerminalCell{});
    std::memset(tabs_,0,sizeof(tabs_));for(unsigned x=8;x<columns_;x+=8)tabs_[x/8]|=static_cast<uint8_t>(1u<<(x%8));
    redraw();return true;
}
void TerminalParser::redraw(){std::memset(storage_.dirty,255,sizeof(storage_.dirty));}
void TerminalParser::mark(unsigned i){if(view_offset_){redraw();return;}if(i>=cells_)return;storage_.dirty[i/8]|=static_cast<uint8_t>(1u<<(i%8));}
void TerminalParser::reset(){
    ++clear_generation_;synchronized_output_=false;
    style_=TerminalCell{};saved_style_=style_;x_=y_=saved_x_=saved_y_=main_x_=main_y_=0;
    top_=0;bottom_=rows_-1;scan_=0;alternate_=origin_=pending_wrap_=application_cursor_=g0_line_=g1_line_=shifted_=false;
    wrap_=cursor_visible_=true;restore_main_style_=false;parse_=Parse::ground;unicode_left_=0;history_head_=history_count_=view_offset_=replacements_=0;
    std::fill_n(storage_.main,cells_,style_);std::fill_n(storage_.alternate,cells_,style_);
    std::fill_n(storage_.history,columns_*terminal_history_rows,style_);
    std::memset(tabs_,0,sizeof(tabs_));for(unsigned i=8;i<columns_;i+=8)tabs_[i/8]|=static_cast<uint8_t>(1u<<(i%8));redraw();
}
TerminalCell TerminalParser::cell(unsigned x,unsigned y)const{
    if(x>=columns_ || y>=rows_)return TerminalCell{};
    if(alternate_ || !view_offset_)return screen()[y*columns_+x];
    const unsigned logical=history_count_-view_offset_+y;
    if(logical<history_count_){const unsigned oldest=(history_head_+terminal_history_rows-history_count_)%terminal_history_rows;return storage_.history[((oldest+logical)%terminal_history_rows)*columns_+x];}
    return storage_.main[(logical-history_count_)*columns_+x];
}
bool TerminalParser::take_dirty(unsigned& x,unsigned& y,TerminalCell& value,bool& cursor){
    for(unsigned searched=0;searched<cells_;++searched){const unsigned i=scan_;scan_=(scan_+1)%cells_;const uint8_t bit=static_cast<uint8_t>(1u<<(i%8));
        if(!(storage_.dirty[i/8]&bit))continue;
        storage_.dirty[i/8]&=static_cast<uint8_t>(~bit);x=i%columns_;y=i/columns_;value=cell(x,y);cursor=cursor_visible_ && !view_offset_ && x==x_ && y==y_;return true;
    }return false;
}
bool TerminalParser::take_dirty_row(unsigned& row,TerminalCell* cells,unsigned capacity,int& cursor_column){
    if(!cells || capacity<columns_)return false;
    for(unsigned searched=0;searched<rows_;++searched){
        const unsigned candidate=scan_/columns_;scan_=((candidate+1)%rows_)*columns_;bool dirty=false;
        for(unsigned x=0;x<columns_;++x){const unsigned i=candidate*columns_+x;if(storage_.dirty[i/8]&(1u<<(i%8))){dirty=true;break;}}
        if(!dirty)continue;
        row=candidate;cursor_column=cursor_visible_ && !view_offset_ && row==y_?static_cast<int>(x_):-1;
        for(unsigned x=0;x<columns_;++x){const unsigned i=row*columns_+x;cells[x]=cell(x,row);storage_.dirty[i/8]&=static_cast<uint8_t>(~(1u<<(i%8)));}
        return true;
    }
    return false;
}
void TerminalParser::scroll_view(int rows){const int64_t maximum=alternate_?0:history_count_;view_offset_=static_cast<unsigned>(std::clamp(static_cast<int64_t>(view_offset_)+rows,int64_t{0},maximum));redraw();}
void TerminalParser::erase(unsigned first,unsigned last){if(first>=cells_)return;last=std::min(last,cells_-1);auto blank=style_;blank.glyph=' ';for(unsigned i=first;i<=last;++i){screen()[i]=blank;mark(i);}}
void TerminalParser::scroll(unsigned first,unsigned last,int amount){
    if(first>last || last>=rows_ || !amount){return;}
    const unsigned count=std::min(static_cast<unsigned>(amount<0?-amount:amount),last-first+1);
    auto* data=screen();const size_t row_bytes=columns_*sizeof(TerminalCell);const unsigned remain=last-first+1-count;
    if(amount>0){
        if(!alternate_ && first==0 && last==rows_-1){for(unsigned n=0;n<count;++n){std::memcpy(storage_.history+history_head_*columns_,data+n*columns_,row_bytes);history_head_=(history_head_+1)%terminal_history_rows;history_count_=std::min(history_count_+1,terminal_history_rows);if(view_offset_)view_offset_=std::min(view_offset_+1,history_count_);}}
        std::memmove(data+first*columns_,data+(first+count)*columns_,remain*row_bytes);erase((last-count+1)*columns_,(last+1)*columns_-1);
    }else{std::memmove(data+(first+count)*columns_,data+first*columns_,remain*row_bytes);erase(first*columns_,(first+count)*columns_-1);}redraw();
}
void TerminalParser::linefeed(){pending_wrap_=false;if(y_==bottom_)scroll(top_,bottom_,1);else if(y_<rows_-1)++y_;}
void TerminalParser::put(uint8_t value){
    if(pending_wrap_ && wrap_){x_=0;linefeed();}pending_wrap_=false;
    if(shifted_?g1_line_:g0_line_){if(value=='q')value='-';else if(value=='x')value='|';else if(std::strchr("jklmnwtuv",value))value='+';}
    auto cell=style_;cell.glyph=value;const unsigned i=y_*columns_+x_;screen()[i]=cell;mark(i);
    if(x_==columns_-1)pending_wrap_=wrap_;else ++x_;
}
void TerminalParser::codepoint(uint32_t value){
    if(value<128){put(static_cast<uint8_t>(value));return;}
    if(value==0x2500 || value==0x2501){put('-');return;}if(value==0x2502 || value==0x2503){put('|');return;}
    if(value>=0x250c && value<=0x257f){put('+');return;}
    ++replacements_;put('?');
    if((value>=0x1100 && value<=0x115f) || (value>=0x2e80 && value<=0xa4cf) ||
       (value>=0xac00 && value<=0xd7a3) || (value>=0xf900 && value<=0xfaff) ||
       (value>=0xff01 && value<=0xff60) || (value>=0x1f300 && value<=0x1faff))put(' ');
}
void TerminalParser::save(){saved_x_=x_;saved_y_=y_;saved_style_=style_;}
void TerminalParser::restore(){x_=saved_x_;y_=saved_y_;style_=saved_style_;pending_wrap_=false;}
int TerminalParser::parameter(unsigned i,int fallback)const{return i>parameter_ || params_[i]<0?fallback:params_[i];}
void TerminalParser::answer(const char* value,size_t size){if(reply_)reply_(context_,value,size);}
void TerminalParser::sgr(){
    for(unsigned i=0;i<=parameter_;++i){const int p=parameter(i,0);
        if(p==0)style_=TerminalCell{};else if(p==1)style_.flags|=bold;else if(p==22)style_.flags&=static_cast<uint8_t>(~bold);
        else if(p==4)style_.flags|=underline;else if(p==24)style_.flags&=static_cast<uint8_t>(~underline);
        else if(p==7)style_.flags|=reverse;else if(p==27)style_.flags&=static_cast<uint8_t>(~reverse);
        else if(p==8)style_.flags|=conceal;else if(p==28)style_.flags&=static_cast<uint8_t>(~conceal);
        else if(p==39)style_.flags|=default_fg;else if(p==49)style_.flags|=default_bg;
        else if((p>=30 && p<=37) || (p>=90 && p<=97)){style_.fg=static_cast<uint8_t>(p>=90?p-90+8:p-30);style_.flags&=static_cast<uint8_t>(~default_fg);}
        else if((p>=40 && p<=47) || (p>=100 && p<=107)){style_.bg=static_cast<uint8_t>(p>=100?p-100+8:p-40);style_.flags&=static_cast<uint8_t>(~default_bg);}
        else if(p==38 || p==48){uint8_t colour=0;bool valid=false;const int kind=parameter(i+1,-1);
            if(kind==5 && i+2<=parameter_){const int n=parameter(i+2,-1);valid=n>=0 && n<=255;colour=static_cast<uint8_t>(n);i+=2;}
            else if(kind==2 && i+4<=parameter_){const int r=parameter(i+2,-1),g=parameter(i+3,-1),b=parameter(i+4,-1);valid=r>=0 && r<=255 && g>=0 && g<=255 && b>=0 && b<=255;if(valid)colour=cube(r,g,b);i+=4;}
            if(valid){if(p==38){style_.fg=colour;style_.flags&=static_cast<uint8_t>(~default_fg);}else{style_.bg=colour;style_.flags&=static_cast<uint8_t>(~default_bg);}}
        }
    }
}
void TerminalParser::csi(uint8_t final){
    if(bad_csi_){return;}
    const unsigned count=static_cast<unsigned>(std::clamp(parameter(0,1),1,static_cast<int>(cells_)));
    const unsigned min_row=origin_?top_:0,max_row=origin_?bottom_:rows_-1;
    if(private_ && (final=='h' || final=='l')){const bool enabled=final=='h';bool sync_only=true;
        for(unsigned i=0;i<=parameter_;++i){const int p=parameter(i,0);if(p!=2026)sync_only=false;
            if(p==1)application_cursor_=enabled;else if(p==6){origin_=enabled;x_=0;y_=enabled?top_:0;}
            else if(p==2026)synchronized_output_=enabled;else if(p==7)wrap_=enabled;else if(p==25)cursor_visible_=enabled;
            else if(p==47 || p==1047 || p==1049){if(enabled && !alternate_){main_x_=x_;main_y_=y_;main_style_=style_;restore_main_style_=p==1049;alternate_=true;view_offset_=0;x_=y_=0;erase(0,cells_-1);}else if(!enabled && alternate_){alternate_=false;x_=main_x_;y_=main_y_;if(restore_main_style_)style_=main_style_;restore_main_style_=false;}redraw();}
        }if(!sync_only)pending_wrap_=false;return;
    }
    if(private_ || (secondary_ && final!='c'))return;
    switch(final){
        case 'A':y_=y_>min_row+count?y_-count:min_row;break;
        case 'B':case 'e':y_=std::min(y_+count,max_row);break;
        case 'C':case 'a':x_=std::min(x_+count,columns_-1);break;
        case 'D':x_=x_>count?x_-count:0;break;
        case 'E':y_=std::min(y_+count,max_row);x_=0;break;
        case 'F':y_=y_>min_row+count?y_-count:min_row;x_=0;break;
        case 'G':case '`':x_=static_cast<unsigned>(std::clamp(parameter(0,1)-1,0,static_cast<int>(columns_)-1));break;
        case 'd':y_=static_cast<unsigned>(std::clamp(parameter(0,1)-1+static_cast<int>(min_row),static_cast<int>(min_row),static_cast<int>(max_row)));break;
        case 'H':case 'f':y_=static_cast<unsigned>(std::clamp(parameter(0,1)-1+static_cast<int>(min_row),static_cast<int>(min_row),static_cast<int>(max_row)));x_=static_cast<unsigned>(std::clamp(parameter(1,1)-1,0,static_cast<int>(columns_)-1));break;
        case 'J':{const int mode=parameter(0,0);if(mode>=0 && mode<=2)++clear_generation_;const unsigned pos=y_*columns_+x_;if(mode==0)erase(pos,cells_-1);else if(mode==1)erase(0,pos);else if(mode==2)erase(0,cells_-1);else if(mode==3){history_count_=history_head_=view_offset_=0;redraw();}break;}
        case 'K':{const unsigned start=y_*columns_;const int mode=parameter(0,0);if(mode==0)erase(start+x_,start+columns_-1);else if(mode==1)erase(start,start+x_);else if(mode==2)erase(start,start+columns_-1);break;}
        case 'X':erase(y_*columns_+x_,y_*columns_+std::min(x_+count,columns_)-1);break;
        case '@':case 'P':{const unsigned n=std::min(count,columns_-x_),start=y_*columns_+x_;auto* data=screen();if(final=='@'){std::memmove(data+start+n,data+start,(columns_-x_-n)*sizeof(TerminalCell));erase(start,start+n-1);}else{std::memmove(data+start,data+start+n,(columns_-x_-n)*sizeof(TerminalCell));erase((y_+1)*columns_-n,(y_+1)*columns_-1);}redraw();break;}
        case 'L':case 'M':if(y_>=top_ && y_<=bottom_)scroll(y_,bottom_,final=='M'?static_cast<int>(count):-static_cast<int>(count));break;
        case 'S':scroll(top_,bottom_,static_cast<int>(count));break;
        case 'T':scroll(top_,bottom_,-static_cast<int>(count));break;
        case 'r':{const int first=parameter(0,1)-1,last=parameter(1,rows_)-1;if(first>=0 && first<last && last<static_cast<int>(rows_)){top_=static_cast<unsigned>(first);bottom_=static_cast<unsigned>(last);x_=0;y_=origin_?top_:0;}break;}
        case 'm':sgr();break;
        case 's':save();break;case 'u':restore();break;
        case 'g':if(parameter(0,0)==3)std::memset(tabs_,0,sizeof(tabs_));else if(parameter(0,0)==0)tabs_[x_/8]&=static_cast<uint8_t>(~(1u<<(x_%8)));break;
        case 'n':if(parameter(0,0)==5)answer("\x1b[0n",4);else if(parameter(0,0)==6){char reply[24];const int n=std::snprintf(reply,sizeof(reply),"\x1b[%u;%uR",y_+1-(origin_?top_:0),x_+1);answer(reply,static_cast<size_t>(n));}break;
        case 'c':if(secondary_)answer("\x1b[>0;0;0c",9);else answer("\x1b[?1;2c",7);break;
        case 't':if(parameter(0,0)==18){char reply[24];const int n=std::snprintf(reply,sizeof(reply),"\x1b[8;%u;%ut",rows_,columns_);answer(reply,static_cast<size_t>(n));}break;
        default:break;
    }pending_wrap_=false;
}
void TerminalParser::byte(uint8_t value){
    if(parse_==Parse::osc || parse_==Parse::ignore_string){if(value==27)parse_=parse_==Parse::osc?Parse::osc_escape:Parse::ignore_escape;else if(value==7 && parse_==Parse::osc)parse_=Parse::ground;return;}
    if(parse_==Parse::osc_escape || parse_==Parse::ignore_escape){if(value=='\\')parse_=Parse::ground;else if(value!=27)parse_=parse_==Parse::osc_escape?Parse::osc:Parse::ignore_string;return;}
    if(value==27){parse_=Parse::escape;unicode_left_=0;return;}
    if(value==24 || value==26){parse_=Parse::ground;unicode_left_=0;return;}
    if(parse_==Parse::charset){if(charset_slot_==0)g0_line_=value=='0';else g1_line_=value=='0';parse_=Parse::ground;return;}
    if(parse_==Parse::escape){parse_=Parse::ground;
        if(value=='['){parse_=Parse::csi;std::fill_n(params_,16,-1);parameter_=0;private_=secondary_=bad_csi_=false;}
        else if(value==']')parse_=Parse::osc;else if(value=='P' || value=='^' || value=='_')parse_=Parse::ignore_string;
        else if(value=='(' || value==')'){charset_slot_=value==')'?1:0;parse_=Parse::charset;}else if(value=='7')save();else if(value=='8')restore();
        else if(value=='D')linefeed();else if(value=='E'){x_=0;linefeed();}else if(value=='M'){if(y_==top_)scroll(top_,bottom_,-1);else if(y_)--y_;}
        else if(value=='H')tabs_[x_/8]|=static_cast<uint8_t>(1u<<(x_%8));else if(value=='c')reset();
        return;
    }
    if(parse_==Parse::csi){
        if(value=='?' && parameter_==0 && params_[0]<0)private_=true;
        else if(value=='>' && parameter_==0 && params_[0]<0)secondary_=true;
        else if(value>='0' && value<='9'){if(params_[parameter_]<0)params_[parameter_]=0;if(params_[parameter_]>1000)bad_csi_=true;else params_[parameter_]=params_[parameter_]*10+value-'0';}
        else if(value==';'){if(parameter_==15)bad_csi_=true;else ++parameter_;}
        else if(value>=0x40 && value<=0x7e){csi(value);parse_=Parse::ground;}
        else if(value>=0x20)bad_csi_=true;
        return;
    }
    if(value<32 || value==127){if(unicode_left_){++replacements_;put('?');unicode_left_=0;}
        if(value==14)shifted_=true;else if(value==15)shifted_=false;
        else if(value==13){x_=0;pending_wrap_=false;}else if(value==10 || value==11 || value==12)linefeed();
        else if(value==8){if(x_)--x_;pending_wrap_=false;}else if(value==9){pending_wrap_=false;do{if(x_==columns_-1)break;++x_;}while(!(tabs_[x_/8]&(1u<<(x_%8))));}
        return;
    }
    if(unicode_left_){if((value&0xc0)==0x80){unicode_=(unicode_<<6)|(value&0x3f);if(--unicode_left_==0){if(unicode_<unicode_min_ || unicode_>0x10ffff || (unicode_>=0xd800 && unicode_<=0xdfff)){++replacements_;put('?');}else codepoint(unicode_);}return;}++replacements_;put('?');unicode_left_=0;}
    if(value<128){put(value);return;}
    if(value>=0xc2 && value<=0xdf){unicode_=value&0x1f;unicode_left_=1;unicode_min_=0x80;}
    else if(value>=0xe0 && value<=0xef){unicode_=value&0xf;unicode_left_=2;unicode_min_=0x800;}
    else if(value>=0xf0 && value<=0xf4){unicode_=value&7;unicode_left_=3;unicode_min_=0x10000;}
    else{++replacements_;put('?');}
}
void TerminalParser::feed(const uint8_t* data,size_t length){if(!data && length)return;mark(y_*columns_+x_);for(size_t i=0;i<length;++i)byte(data[i]);mark(y_*columns_+x_);}
}
