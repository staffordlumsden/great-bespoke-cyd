#pragma once
#include <cstddef>
#include <cstdint>
namespace cm {
enum class LocalKeyAction : uint8_t {none,zoom_in,zoom_out,scroll_older,scroll_newer};
struct KeySequence { char bytes[16]{}; size_t size=0;LocalKeyAction local=LocalKeyAction::none; };
// US HID keyboard usages. No raw reports or typed bytes are logged.
bool map_key(uint8_t usage,uint8_t modifiers,bool caps,bool num,bool application_cursor,KeySequence& out,bool local_zoom=false);
using KeySink=void(*)(void*,const KeySequence&);
class BootKeyboard {
public:
    bool report(const uint8_t* data,size_t size,uint64_t now,KeySink sink,void* context);
    void tick(uint64_t now,KeySink sink,void* context);
    void reset();
    void application_cursor(bool value){application_cursor_=value;}
    void local_zoom(bool value){local_zoom_=value;}
    uint8_t modifiers()const{return modifiers_;}
private:
    uint8_t keys_[6]{},modifiers_=0,repeat_=0;
    bool caps_=false,num_=true,application_cursor_=false,local_zoom_=false;
    uint64_t repeat_due_=0;
};
}
