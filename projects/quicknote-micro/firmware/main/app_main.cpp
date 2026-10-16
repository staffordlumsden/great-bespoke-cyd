#include "bluetooth_keyboard.h"
#include "console_bridge.h"
#include "cyd_clock.h"
#include "cyd_hal.h"
#include "note_model.h"
#include "quicknote_ui.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char* notes_root="/sdcard/files/QuickNote";
quicknote::Editor editor{};
quicknote::ui::Scene scene{};
bool light_mode=false,history_mode=false,clear_armed=false,pending_save=false,render_needed=true;
int font_index=1,day_offset=0,history_scroll=0;
char history[quicknote::editor_capacity+1]{},status_text[96]{};
size_t history_size=0;
uint64_t status_until=0,draft_due=0,last_second=~uint64_t(0),clear_until=0;

void status(const char* value,uint64_t now,unsigned duration=5000){std::snprintf(status_text,sizeof(status_text),"%s",value);status_until=duration?now+duration:0;render_needed=true;}
bool day_details(const cyd::clock::Snapshot& now,int offset,char (&iso)[11],char (&label)[32]) {
    if(!now.valid)return false;
    cyd::clock::Civil noon=now.civil;noon.hour=12;noon.minute=noon.second=0;int64_t epoch=0;
    if(!cyd::clock::local_to_utc(noon,epoch))return false;
    epoch+=int64_t(offset)*86400;
    cyd::clock::Civil selected{};char zone[12]{};if(!cyd::clock::utc_to_local(epoch,selected,label,zone))return false;
    std::snprintf(iso,sizeof(iso),"%04d-%02d-%02d",selected.year,selected.month,selected.day);return true;
}
void load_history(uint64_t now) {
    const auto clock=cyd::clock::snapshot();char iso[11],label[32],error[80]{};
    history_scroll=0;history[0]=0;history_size=0;
    if(!day_details(clock,day_offset,iso,label)){status("Set the clock in Console before browsing",now,0);return;}
    bool truncated=false;history_size=quicknote::load_day_tail(notes_root,iso,history,sizeof(history),truncated,error,sizeof(error));
    if(error[0])status(error,now,0);else if(truncated)status("Showing the most recent part of this day",now,0);else status(label,now,3000);
    render_needed=true;
}
void save_settings() {
    nvs_handle_t handle=0;if(nvs_open("qn_ui",NVS_READWRITE,&handle)!=ESP_OK)return;
    nvs_set_u8(handle,"light",light_mode);nvs_set_u8(handle,"font",font_index);nvs_commit(handle);nvs_close(handle);
}
void load_settings() {
    nvs_handle_t handle=0;if(nvs_open("qn_ui",NVS_READONLY,&handle)!=ESP_OK)return;uint8_t value=0;
    if(nvs_get_u8(handle,"light",&value)==ESP_OK)light_mode=value!=0;
    if(nvs_get_u8(handle,"font",&value)==ESP_OK&&value<3)font_index=value;
    nvs_close(handle);
}
const char* keyboard_label(const cm::KeyboardSnapshot& keyboard) {
    using cm::KeyboardState;switch(keyboard.state){case KeyboardState::connected:return "BT OK";case KeyboardState::searching:return "SCAN";
        case KeyboardState::found:return "PAIR";case KeyboardState::pairing:return "PAIRING";case KeyboardState::error:return "BT ERR";
        case KeyboardState::starting:return "BT START";case KeyboardState::disconnected:return keyboard.saved?"BT WAKE":"BT SCAN";default:return "BT OFF";}
}
void render() {
    const auto clock=cyd::clock::snapshot();const auto keyboard=cm::keyboard_snapshot();
    char time[8]="--:--",date[32]="Clock not set";if(clock.valid){std::snprintf(time,sizeof(time),"%02d:%02d",clock.civil.hour,clock.civil.minute);std::snprintf(date,sizeof(date),"%s",clock.date);}
    char automatic[96]{};const char* message=status_text;
    if(!message[0]) {
        if(!clock.valid)message="Set clock in Console before saving";
        else if(history_mode)message="Tap the note area to scroll";
        else if(editor.dirty())message="Unsaved draft - Ctrl+S saves";
        else message="Type a note. Enter adds a new line.";
    }
    if(keyboard.show_passkey){if(keyboard.numeric_confirmation)std::snprintf(automatic,sizeof(automatic),"Tap BT to accept %06lu",static_cast<unsigned long>(keyboard.passkey));else std::snprintf(automatic,sizeof(automatic),"Type %06lu on the B1, then Enter",static_cast<unsigned long>(keyboard.passkey));message=automatic;}
    quicknote::ui::Model model{};model.light=light_mode;model.history=history_mode;model.clock_valid=clock.valid;model.dirty=editor.dirty();
    model.save_enabled=!history_mode&&editor.size()&&clock.valid;model.bluetooth_connected=keyboard.state==cm::KeyboardState::connected;
    model.button_clear_armed=clear_armed;model.font_index=font_index;model.history_scroll=history_scroll;model.day_offset=day_offset;
    char selected_date[32]{};char selected_iso[11]{};
    if(history_mode&&clock.valid&&day_details(clock,day_offset,selected_iso,selected_date))model.date=selected_date;else model.date=date;
    model.time=time;model.bluetooth=keyboard_label(keyboard);model.status=message;
    model.content=history_mode?history:editor.data();model.content_size=history_mode?history_size:editor.size();model.cursor=editor.cursor();
    quicknote::ui::build(scene,model);const auto result=cyd::hal_render(quicknote::ui::pixel,&scene);
    if(result!=ESP_OK)std::printf("QN_UI render_error=%s\n",esp_err_to_name(result));
    render_needed=false;
}
bool checkpoint(uint64_t now,bool announce) {
    char error[80]{};if(quicknote::checkpoint_draft(notes_root,editor,error,sizeof(error))){draft_due=0;if(announce)status("Draft checkpoint saved",now,2500);return true;}
    draft_due=now+5000;status(error,now,0);return false;
}
void save_note(uint64_t now) {
    const auto clock=cyd::clock::snapshot();if(!clock.valid){status("Set the clock in Console before saving",now,0);return;}
    if(!editor.size()){status("Write something before saving",now,2500);return;}
    if(editor.dirty()&&!checkpoint(now,false))return;
    char iso[11],label[32],time[8],error[80]{};if(!day_details(clock,0,iso,label)){status("Could not resolve today's date",now,0);return;}
    std::snprintf(time,sizeof(time),"%02d:%02d",clock.civil.hour,clock.civil.minute);
    if(!quicknote::append_note(notes_root,iso,label,time,editor,error,sizeof(error))){status(error,now,0);return;}
    const bool cleaned=quicknote::discard_draft(notes_root,error,sizeof(error));editor.clear();editor.mark_saved();draft_due=0;
    if(!cleaned){status("Note saved; old draft cleanup failed",now,0);std::printf("QN_NOTE saved date=%s cleanup=FAILED bytes=REDACTED\n",iso);return;}
    char saved[64];std::snprintf(saved,sizeof(saved),"Saved to %s.md",iso);status(saved,now,4500);std::printf("QN_NOTE saved date=%s bytes=REDACTED\n",iso);
}
void keyboard_input(void*,const cm::KeySequence& key) {
    if(key.local==cm::LocalKeyAction::zoom_in||key.local==cm::LocalKeyAction::zoom_out){
        const int direction=key.local==cm::LocalKeyAction::zoom_in?1:-1;
        const int next=std::clamp(font_index+direction,0,2);
        if(next!=font_index){font_index=next;history_scroll=0;save_settings();render_needed=true;}
        return;
    }
    if(history_mode&&(key.local==cm::LocalKeyAction::scroll_older||key.local==cm::LocalKeyAction::scroll_newer)){
        const int page=static_cast<int>(quicknote::ui::visible_rows(font_index));
        history_scroll=std::max(0,history_scroll+(key.local==cm::LocalKeyAction::scroll_older?-page:page));render_needed=true;return;
    }
    if(history_mode){
        if(key.size==3&&key.bytes[0]==27&&(key.bytes[2]=='A'||key.bytes[2]=='B')){history_scroll=std::max(0,history_scroll+(key.bytes[2]=='A'?-1:1));render_needed=true;}
        else if(key.size==1&&key.bytes[0]==27){history_mode=false;render_needed=true;}
        return;
    }
    const auto result=quicknote::apply_key(editor,key);if(result==quicknote::Input::save)pending_save=true;
    else if(result==quicknote::Input::changed){if(editor.dirty())draft_due=cyd::hal_uptime_ms()+1500;status_text[0]=0;render_needed=true;}
}
void bluetooth_action(uint64_t now) {
    const auto keyboard=cm::keyboard_snapshot();
    if(keyboard.numeric_confirmation)cm::keyboard_confirm(true);
    else if(keyboard.state==cm::KeyboardState::found)cm::keyboard_connect(now);
    else if(keyboard.saved)cm::keyboard_connect(now);
    else cm::keyboard_scan(now);
    render_needed=true;
}
bool bottom_hit(int x,int& index){constexpr int starts[]={4,56,108,160,212,264};constexpr int widths[]={48,48,48,48,48,52};for(int i=0;i<6;++i)if(x>=starts[i]&&x<starts[i]+widths[i]){index=i;return true;}return false;}
void go_console(uint64_t now) {
    if(editor.dirty()&&!checkpoint(now,false))return;
    cyd::hal_sd_unmount();if(!quicknote::return_to_console())status("Resident Console is unavailable",now,0);
}
void touch(int x,int y,uint64_t now) {
    if(y>=25&&y<50&&x>=158&&x<246){bluetooth_action(now);return;}
    if(y>=25&&y<50&&x>=250&&x<312){light_mode=!light_mode;save_settings();render_needed=true;return;}
    if(history_mode&&y>=53&&y<186){const int page=quicknote::ui::visible_rows(font_index);history_scroll=std::max(0,history_scroll+(y<120?-page:page));render_needed=true;return;}
    if(y<207||y>=239)return;
    int action=-1;if(!bottom_hit(x,action))return;
    if(action==0){history_mode=!history_mode;day_offset=0;if(history_mode)load_history(now);else{status_text[0]=0;render_needed=true;}}
    else if(action==1){if(history_mode){--day_offset;load_history(now);}else pending_save=true;}
    else if(action==2){if(history_mode){if(day_offset<0){++day_offset;load_history(now);}}else if(editor.size()){if(clear_armed){char error[80]{};if(quicknote::discard_draft(notes_root,error,sizeof(error))){editor.clear();editor.mark_saved();draft_due=0;status("Draft cleared",now,2500);}else status(error,now,0);clear_armed=false;}else{clear_armed=true;clear_until=now+3000;status("Tap CLEAR again to discard the draft",now,3000);}}}
    else if(action==3&&font_index>0){--font_index;history_scroll=0;save_settings();render_needed=true;}
    else if(action==4&&font_index<2){++font_index;history_scroll=0;save_settings();render_needed=true;}
    else if(action==5)go_console(now);
}
}

extern "C" void app_main() {
    esp_log_level_set("*",ESP_LOG_NONE);std::printf("QN_BOOT version=%s storage=MARKDOWN keyboard=BLE fonts=PIXELFONTS\n",esp_app_get_description()->version);
    const auto nvs=nvs_flash_init();const auto board=cyd::hal_init_with_sd(10000,6,2048);cyd::clock::init();load_settings();
    char error[80]{};if(board.sd==ESP_OK&&board.fat32&&quicknote::ensure_note_directories(notes_root,error,sizeof(error))){
        if(!quicknote::load_draft(notes_root,editor,error,sizeof(error)))status(error,0,0);else if(editor.size())status("Recovered your saved draft",0,5000);
    }else if(!error[0])std::snprintf(error,sizeof(error),"SD card is unavailable or not FAT32");
    const bool keyboard=nvs==ESP_OK&&cm::keyboard_start(cyd::hal_uptime_ms());const bool hardware=nvs==ESP_OK&&board.display==ESP_OK&&board.touch==ESP_OK&&board.sd==ESP_OK&&board.fat32;
    quicknote::confirm_first_boot(hardware);std::printf("QN_BOARD display=%d touch=%d sd=%d fat32=%d nvs=%d keyboard=%d heap_kb=%u\n",board.display,board.touch,board.sd,board.fat32,nvs,keyboard,cyd::hal_free_heap_kb());
    if(error[0])status(error,0,0);else if(!keyboard)status("Bluetooth keyboard could not start",0,0);render();
    bool touching=false,touch_armed=false;unsigned released=0,idle_releases=0;int touch_x=0,touch_y=0;
    for(;;){const uint64_t now=cyd::hal_uptime_ms();if(keyboard)cm::keyboard_poll(now,keyboard_input,nullptr);
        if(pending_save){pending_save=false;save_note(now);}
        if(draft_due&&now>=draft_due)checkpoint(now,false);
        if(clear_armed&&now>=clear_until){clear_armed=false;render_needed=true;}
        if(status_until&&now>=status_until){status_until=0;status_text[0]=0;render_needed=true;}
        int x=0,y=0;const bool pressed=cyd::hal_touch_poll(x,y);
        if(!touch_armed){if(pressed)idle_releases=0;else if(++idle_releases>=10)touch_armed=true;}
        else if(pressed){if(!touching){touching=true;touch_x=x;touch_y=y;}released=0;}
        else if(touching&&++released>=3){touching=false;touch(touch_x,touch_y,now);}
        const uint64_t second=now/1000;if(second!=last_second&&!touching){last_second=second;render_needed=true;}
        if(render_needed)render();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
