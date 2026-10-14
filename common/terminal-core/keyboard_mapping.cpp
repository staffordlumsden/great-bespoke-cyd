#include "keyboard_mapping.h"
#include <cstdio>
#include <cstring>
namespace cm {
namespace {
bool has(const uint8_t* keys,uint8_t key){for(unsigned i=0;i<6;++i)if(keys[i]==key)return true;return false;}
bool text(KeySequence& out,const char* s) {
    const size_t n=std::strlen(s);if(n>sizeof(out.bytes))return false;
    std::memcpy(out.bytes,s,n);out.size=n;return true;
}
bool formatted(KeySequence& out,const char* format,unsigned a,unsigned b) {
    const int n=std::snprintf(out.bytes,sizeof(out.bytes),format,a,b);
    if(n<0 || static_cast<size_t>(n)>=sizeof(out.bytes))return false;
    out.size=static_cast<size_t>(n);return true;
}
}
bool map_key(uint8_t key,uint8_t mods,bool caps,bool num,bool app,KeySequence& out,bool local_zoom) {
    out={};
    if(local_zoom && (mods&0x22) && !(mods&0xdd) && (key==75 || key==78)){out.local=key==75?LocalKeyAction::scroll_older:LocalKeyAction::scroll_newer;return true;}
    if(local_zoom && (mods&0x11) && !(mods&0xcc) && (key==45 || key==46 || key==86 || key==87)){out.local=(key==46 || key==87)?LocalKeyAction::zoom_in:LocalKeyAction::zoom_out;return true;}const bool shift=(mods&0x22)!=0,ctrl=(mods&0x11)!=0,alt=(mods&0x44)!=0;
    // Super/GUI has no portable terminal character representation.
    if(mods&0x88)return false;
    const unsigned modifier=1+(shift?1:0)+(alt?2:0)+(ctrl?4:0);
    char final=0;
    switch(key){case 79:final='C';break;case 80:final='D';break;case 81:final='B';break;case 82:final='A';break;case 74:final='H';break;case 77:final='F';break;}
    if(final) {
        if(modifier==1) {char s[]={27,app?'O':'[',final,0};return text(out,s);}
        const int n=std::snprintf(out.bytes,sizeof(out.bytes),"\033[1;%u%c",modifier,final);
        if(n<0 || static_cast<size_t>(n)>=sizeof(out.bytes))return false;
        out.size=static_cast<size_t>(n);return true;
    }
    unsigned tilde=0;
    switch(key){case 73:tilde=2;break;case 76:tilde=3;break;case 75:tilde=5;break;case 78:tilde=6;break;}
    constexpr unsigned functions[]={15,17,18,19,20,21,23,24};
    if(key>=62 && key<=69)tilde=functions[key-62];
    if(tilde)return modifier==1?formatted(out,"\033[%u~",tilde,0):formatted(out,"\033[%u;%u~",tilde,modifier);
    if(key>=58 && key<=61) {
        const char f=static_cast<char>('P'+key-58);
        if(modifier==1){char s[]={27,'O',f,0};return text(out,s);}
        const int n=std::snprintf(out.bytes,sizeof(out.bytes),"\033[1;%u%c",modifier,f);
        if(n<0 || static_cast<size_t>(n)>=sizeof(out.bytes))return false;
        out.size=static_cast<size_t>(n);return true;
    }
    if(key==43 && shift)return text(out,"\033[Z");
    char c=0;bool scalar=true;
    if(key>=4 && key<=29)c=static_cast<char>((shift!=caps?'A':'a')+key-4);
    else if(key>=30 && key<=39)c=(shift?"!@#$%^&*()":"1234567890")[key-30];
    else if(key>=45 && key<=56)c=(shift?"_+{}|~:\"~<>?":"-=[]\\#;'`,./")[key-45];
    else if(key==40 || key==88)c='\r';
    else if(key==41)c=27;
    else if(key==42)c=ctrl?8:127;
    else if(key==43)c='\t';
    else if(key==44)c=' ';
    else if(key>=84 && key<=87)c="/*-+"[key-84];
    else if(key>=89 && key<=99) {
        if(num)c="1234567890."[key-89];
        else {
            constexpr uint8_t navigation[]={77,81,78,80,0,79,74,82,75,73,76};
            return navigation[key-89] && map_key(navigation[key-89],mods,caps,num,app,out);
        }
    } else scalar=false;
    if(!scalar)return false;
    if(ctrl && key!=40 && key!=41 && key!=42 && key!=43 && key!=88) {
        if(c>='a' && c<='z')c=static_cast<char>(c-'a'+1);
        else if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+1);
        else if(c==' ' || c=='@' || c=='2')c=0;
        else if(c=='[')c=27;else if(c=='\\')c=28;else if(c==']')c=29;
        else if(c=='^' || c=='6')c=30;else if(c=='_' || c=='-')c=31;
        else return false;
    }
    if(alt)out.bytes[out.size++]=27;
    out.bytes[out.size++]=c;return true;
}
void BootKeyboard::reset(){const bool local_zoom=local_zoom_;*this=BootKeyboard{};local_zoom_=local_zoom;}
bool BootKeyboard::report(const uint8_t* data,size_t size,uint64_t now,KeySink sink,void* context) {
    if(!data || size!=8 || data[1]!=0){repeat_=0;return false;}
    for(unsigned i=2;i<8;++i)if(data[i] && data[i]<4){repeat_=0;return false;}
    modifiers_=data[0];const uint8_t* next=data+2;
    if(repeat_ && !has(next,repeat_))repeat_=0;
    for(unsigned i=0;i<6;++i) {
        const uint8_t key=next[i];if(!key || has(keys_,key))continue;
        bool duplicate=false;for(unsigned j=0;j<i;++j)if(next[j]==key)duplicate=true;
        if(duplicate)continue;
        if(key==57){caps_=!caps_;continue;}
        if(key==83){num_=!num_;continue;}
        KeySequence value{};
        if(map_key(key,modifiers_,caps_,num_,application_cursor_,value,local_zoom_)) {
            if(sink)sink(context,value);
            repeat_=value.local==LocalKeyAction::none?key:0;repeat_due_=now+450;
        }
    }
    std::memcpy(keys_,next,6);return true;
}
void BootKeyboard::tick(uint64_t now,KeySink sink,void* context) {
    if(!repeat_ || now<repeat_due_)return;
    KeySequence value{};if(map_key(repeat_,modifiers_,caps_,num_,application_cursor_,value,local_zoom_) && sink)sink(context,value);
    repeat_due_=now+50; // At most one repeat per tick; no burst after a slow frame.
}
}
