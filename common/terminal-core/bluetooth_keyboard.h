#pragma once
#include "keyboard_mapping.h"
namespace cm {
enum class KeyboardState {off,starting,disconnected,searching,found,pairing,connected,error};
struct KeyboardSnapshot {
    KeyboardState state=KeyboardState::off;
    unsigned candidates=0,reports=0,keys=0,rejected=0;
    bool saved=false,bonded=false,show_passkey=false,numeric_confirmation=false;
    uint32_t passkey=0;unsigned auth_mode=0;
    int error=0;unsigned worker_stack_min=0,callback_stack_min=0;
};
bool keyboard_start(uint64_t now);
void keyboard_poll(uint64_t now,KeySink sink,void* context);
void keyboard_scan(uint64_t now);
void keyboard_connect(uint64_t now);
void keyboard_disconnect();
void keyboard_forget(); // UI must confirm before calling.
void keyboard_confirm(bool accept);
KeyboardSnapshot keyboard_snapshot();
const char* keyboard_state_name(KeyboardState state);
}
