#include "keyboard_report.h"
namespace cm {
bool canonical_keyboard_report(const uint8_t* map,size_t size,uint8_t report_id) {
    if(!map || !size || size>2048)return false;
    struct Global {uint32_t page=0,bits=0,count=0,id=0,minimum=0,maximum=0;};
    Global g{},stack[4]{};unsigned depth=0,collections=0;
    uint32_t low=0,high=0;bool range=false,local_other=false;
    unsigned offset=0,fields=0;bool valid=true;
    for(size_t pos=0;pos<size;) {
        const uint8_t head=map[pos++];
        if(head==0xfe)return false; // Long items are outside this bounded subset.
        const unsigned bytes=(head&3)==3?4:head&3;
        if(bytes>size-pos)return false;
        uint32_t value=0;for(unsigned i=0;i<bytes;++i)value|=uint32_t(map[pos++])<<(8*i);
        const unsigned type=(head>>2)&3,tag=head>>4;
        if(type==1) {
            switch(tag) {
                case 0:g.page=value;break;case 1:g.minimum=value;break;case 2:g.maximum=value;break;
                case 7:g.bits=value;break;case 8:if(!value || value>255)return false;g.id=value;break;
                case 9:g.count=value;break;
                case 10:if(depth==4)return false;stack[depth++]=g;break;
                case 11:if(!depth)return false;g=stack[--depth];break;
                default:break;
            }
        } else if(type==2) {
            if(tag==1){low=value;range=true;}
            else if(tag==2)high=value;
            else local_other=true;
        } else if(type==0) {
            if(tag==10){if(++collections>16)return false;}
            else if(tag==12){if(!collections)return false;--collections;}
            else if(tag==8 && g.id==report_id) {
                if(g.bits>32 || !g.count || g.count>256)return false;
                const unsigned total=g.bits*g.count;
                if(total>64 || offset+total>64)return false;
                // Exact modifier bit order, reserved byte, then six key usages.
                if(offset==0 && total==8 && g.bits==1 && g.count==8 && value==2 &&
                   g.page==7 && range && !local_other && low==0xe0 && high==0xe7 &&
                   g.minimum==0 && g.maximum==1)++fields;
                else if(offset==8 && total==8 && (value&1))++fields;
                else if(offset==16 && total==48 && g.bits==8 && g.count==6 && value==0 &&
                        g.page==7 && range && !local_other && low==0 && high>=0x65 && high<=255 &&
                        g.minimum==0 && g.maximum>=0x65 && g.maximum<=255)++fields;
                else valid=false;
                offset+=total;
            }
            // Local items apply to the next main item only, including output.
            low=high=0;range=false;local_other=false;
        } else return false;
    }
    return valid && !depth && !collections && offset==64 && fields==3;
}
}
