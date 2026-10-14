#include "sdkconfig.h"
#include "bluetooth_keyboard.h"
#include "keyboard_report.h"
#include "config_store.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_hidh.h"
#include "esp_hidh_bluedroid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
namespace cm {
namespace {
constexpr char target_name[]="Model B1";
enum class Kind {params,scan_done,match,opened,closed,input,auth,passkey,numeric,open_done,forget_done};
struct Event {
    Kind kind;int status=0;esp_hidh_dev_t* dev=nullptr;
    uint8_t address[6]{},addr_type=0,map=0;uint16_t report_id=0,length=0;
    uint8_t data[8]{};uint32_t number=0;
};
struct Peer {uint8_t version=1,address[6]{},type=0;};
struct AllowedReport {uint8_t map;uint16_t id;};
StaticQueue_t event_control{},work_control{};
uint8_t event_storage[32*sizeof(Event)]{},work_storage[sizeof(Peer)]{};
QueueHandle_t events=nullptr,work=nullptr;TaskHandle_t worker_handle=nullptr;
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
Peer permitted{},saved_peer{},candidate{};
bool permit=false,dropped=false,worker_busy=false;
unsigned callback_low=0,worker_low=0;
KeyboardSnapshot view{};BootKeyboard decoder{};
esp_hidh_dev_t* device=nullptr;esp_hidh_dev_t* closing=nullptr;
AllowedReport allowed[8]{};unsigned allowed_count=0;
uint64_t deadline=0,retry_due=0;unsigned retries=0;
bool initialized=false,auth_ok=false,manual_off=false,forget_pending=false;

void post(const Event& e) {
    if(xQueueSend(events,&e,0)!=pdTRUE) {
        portENTER_CRITICAL(&lock);dropped=true;portEXIT_CRITICAL(&lock);
    }
}
bool selected(const uint8_t* address) {
    portENTER_CRITICAL(&lock);
    const bool result=permit && !std::memcmp(permitted.address,address,6);
    portEXIT_CRITICAL(&lock);return result;
}
void set_permission(const Peer* peer) {
    portENTER_CRITICAL(&lock);permit=peer!=nullptr;
    if(peer)permitted=*peer;
    else wipe(&permitted,sizeof(permitted));
    portEXIT_CRITICAL(&lock);
}
void fail(int code) {
    view.state=KeyboardState::error;view.error=code;view.show_passkey=false;
    view.numeric_confirmation=false;decoder.reset();manual_off=true;
    set_permission(nullptr);esp_ble_gap_stop_scanning();
    if(device)esp_hidh_dev_close(device);
    std::printf("M2 keyboard error=%d input=REDACTED\n",code);
}
bool check(esp_err_t result,const char* stage) {
    std::printf("M2 keyboard stage=%s result=%d identifiers=REDACTED\n",stage,result);
    if(result==ESP_OK)return true;
    fail(result);return false;
}
bool name_matches(const uint8_t* data,size_t size) {
    if(!data)return false;
    if(size && data[size-1]==0)--size;
    return size==sizeof(target_name)-1 && !std::memcmp(data,target_name,size);
}
void gap_callback(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t* p) {
    if(event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)post({Kind::params,p->scan_param_cmpl.status});
    else if(event==ESP_GAP_BLE_SCAN_START_COMPLETE_EVT && p->scan_start_cmpl.status)
        post({Kind::scan_done,p->scan_start_cmpl.status});
    else if(event==ESP_GAP_BLE_SCAN_RESULT_EVT) {
        auto& r=p->scan_rst;
        if(r.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT)post({Kind::scan_done});
        else if(r.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t length=0;
            auto* name=esp_ble_resolve_adv_data_by_type(r.ble_adv,r.adv_data_len+r.scan_rsp_len,ESP_BLE_AD_TYPE_NAME_CMPL,&length);
            if(!name)name=esp_ble_resolve_adv_data_by_type(r.ble_adv,r.adv_data_len+r.scan_rsp_len,ESP_BLE_AD_TYPE_NAME_SHORT,&length);
            if(name_matches(name,length)) {
                Event e{Kind::match};std::memcpy(e.address,r.bda,6);e.addr_type=r.ble_addr_type;post(e);
            }
        }
    } else if(event==ESP_GAP_BLE_SEC_REQ_EVT)
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,selected(p->ble_security.ble_req.bd_addr));
    else if(event==ESP_GAP_BLE_PASSKEY_REQ_EVT) {
        // Display-only host cannot safely enter a peer-displayed passkey.
        esp_ble_passkey_reply(p->ble_security.ble_req.bd_addr,false,0);
        if(selected(p->ble_security.ble_req.bd_addr))post({Kind::auth,ESP_ERR_NOT_SUPPORTED});
    } else if(event==ESP_GAP_BLE_PASSKEY_NOTIF_EVT || event==ESP_GAP_BLE_NC_REQ_EVT) {
        if(selected(p->ble_security.key_notif.bd_addr)) {
            Event e{event==ESP_GAP_BLE_NC_REQ_EVT?Kind::numeric:Kind::passkey};
            std::memcpy(e.address,p->ble_security.key_notif.bd_addr,6);
            e.number=p->ble_security.key_notif.passkey;post(e);
        } else if(event==ESP_GAP_BLE_NC_REQ_EVT)esp_ble_confirm_reply(p->ble_security.key_notif.bd_addr,false);
    } else if(event==ESP_GAP_BLE_AUTH_CMPL_EVT && selected(p->ble_security.auth_cmpl.bd_addr)) {
        Event e{Kind::auth,p->ble_security.auth_cmpl.success?0:(p->ble_security.auth_cmpl.fail_reason?p->ble_security.auth_cmpl.fail_reason:ESP_FAIL)};
        e.number=p->ble_security.auth_cmpl.auth_mode;post(e);
    } else if(event==ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT)post({Kind::forget_done,p->remove_bond_dev_cmpl.status});
    // Never log key events, passkeys, peer identities or input payloads.
}
void hid_callback(void*,esp_event_base_t,int32_t id,void* data) {
    const unsigned stack=uxTaskGetStackHighWaterMark(nullptr);
    portENTER_CRITICAL(&lock);if(!callback_low || stack<callback_low)callback_low=stack;portEXIT_CRITICAL(&lock);
    auto* p=static_cast<esp_hidh_event_data_t*>(data);
    if(id==ESP_HIDH_OPEN_EVENT)post({Kind::opened,p->open.status,p->open.dev});
    else if(id==ESP_HIDH_CLOSE_EVENT)post({Kind::closed,p->close.reason,p->close.dev});
    else if(id==ESP_HIDH_INPUT_EVENT && p->input.usage==ESP_HID_USAGE_KEYBOARD) {
        Event e{Kind::input,0,p->input.dev};e.map=p->input.map_index;
        e.report_id=p->input.report_id;e.length=p->input.length;
        if(e.length==8)std::memcpy(e.data,p->input.data,8);
        post(e);
    }
}
void worker(void*) {
    Peer request{};
    for(;;) {
        if(xQueueReceive(work,&request,portMAX_DELAY)!=pdTRUE)continue;
        auto* opened=esp_hidh_dev_open(request.address,ESP_HID_TRANSPORT_BLE,request.type);
        const unsigned stack=uxTaskGetStackHighWaterMark(nullptr);
        portENTER_CRITICAL(&lock);worker_busy=false;worker_low=stack;portEXIT_CRITICAL(&lock);
        post({Kind::open_done,opened?0:ESP_FAIL});wipe(&request,sizeof(request));
    }
}
bool has_bond(const Peer& peer) {
    int count=esp_ble_get_bond_device_num();
    if(count<1 || count>8)return false;
    esp_ble_bond_dev_t bonds[8]{};bool found=false;
    if(esp_ble_get_bond_device_list(&count,bonds)==ESP_OK)
        for(int i=0;i<count;++i)found=found || !std::memcmp(bonds[i].bd_addr,peer.address,6);
    wipe(bonds,sizeof(bonds));return found;
}
bool save_peer() {
    nvs_handle_t handle=0;esp_err_t result=nvs_open("cm_keyboard",NVS_READWRITE,&handle);
    if(result==ESP_OK){result=nvs_set_blob(handle,"peer",&permitted,sizeof(Peer));if(result==ESP_OK)result=nvs_commit(handle);nvs_close(handle);}
    if(!check(result,"SAVE_BONDED_PEER"))return false;
    saved_peer=permitted;view.saved=true;return true;
}
bool inspect_reports(esp_hidh_dev_t* dev) {
    size_t count=0,map_count=0;esp_hid_report_item_t* reports=nullptr;esp_hid_raw_report_map_t* maps=nullptr;
    allowed_count=0;
    if(esp_hidh_dev_reports_get(dev,&count,&reports)!=ESP_OK)return false;
    const bool got_maps=esp_hidh_dev_report_maps_get(dev,&map_count,&maps)==ESP_OK;
    for(size_t i=0;i<count;++i) {
        const auto& r=reports[i];
        // Descriptor metadata alone never establishes a report-mode layout.
        if(r.usage!=ESP_HID_USAGE_KEYBOARD || r.report_type!=ESP_HID_REPORT_TYPE_INPUT)continue;
        // This pinned ESP HID BLE backend subscribes report-mode inputs only.
        // Do not count an unsubscribed boot characteristic as usable input.
        bool canonical=false;
        if(r.protocol_mode==ESP_HID_PROTOCOL_MODE_REPORT && got_maps && r.map_index<map_count)
            canonical=canonical_keyboard_report(maps[r.map_index].data,maps[r.map_index].len,r.report_id);
        std::printf("M2 keyboard report map=%u id=%u protocol=%u metadata_bytes=%u canonical=%d\n",r.map_index,r.report_id,r.protocol_mode,r.value_len,canonical);
        if(canonical && allowed_count<8)allowed[allowed_count++]={r.map_index,r.report_id};
    }
    std::free(reports);return allowed_count>0;
}
void connection_ready() {
    if(!device || !auth_ok || view.state==KeyboardState::connected)return;
    if(!has_bond(permitted)){fail(ESP_ERR_INVALID_STATE);return;}
    view.bonded=true;
    if(!view.saved && !save_peer())return;
    view.state=KeyboardState::connected;view.show_passkey=false;view.numeric_confirmation=false;
    retries=0;decoder.reset();std::printf("M2 keyboard connected bonded=1 auth_mode=%u payload=REDACTED\n",view.auth_mode);
}
void begin_open(const Peer& peer,uint64_t now,bool reconnect) {
    portENTER_CRITICAL(&lock);const bool busy=worker_busy;if(!busy)worker_busy=true;portEXIT_CRITICAL(&lock);
    if(busy)return;
    set_permission(&peer);view.state=KeyboardState::pairing;view.error=0;
    view.show_passkey=false;view.numeric_confirmation=false;view.bonded=false;auth_ok=false;
    deadline=now+45000;manual_off=false;decoder.reset();
    std::printf("M2 keyboard action=%s peer=REDACTED\n",reconnect?"RECONNECT":"PAIR_SELECTED");
    if(xQueueSend(work,&peer,0)!=pdTRUE)fail(ESP_ERR_NO_MEM);
}
}
bool keyboard_start(uint64_t now) {
    if(initialized)return false;
    events=xQueueCreateStatic(32,sizeof(Event),event_storage,&event_control);
    work=xQueueCreateStatic(1,sizeof(Peer),work_storage,&work_control);
    view.state=KeyboardState::starting;
    // The user's dual-radio probe found BLE. This image frees Classic memory.
    if(!check(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT),"RELEASE_CLASSIC"))return false;
    esp_bt_controller_config_t controller=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if(!check(esp_bt_controller_init(&controller),"CONTROLLER_INIT") ||
       !check(esp_bt_controller_enable(ESP_BT_MODE_BLE),"CONTROLLER_ENABLE"))return false;
    esp_bluedroid_config_t host=BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if(!check(esp_bluedroid_init_with_cfg(&host),"BLUEDROID_INIT") || !check(esp_bluedroid_enable(),"BLUEDROID_ENABLE") ||
       !check(esp_ble_gap_register_callback(gap_callback),"BLE_GAP") ||
       !check(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler),"GATTC"))return false;
    uint8_t auth=ESP_LE_AUTH_REQ_SC_MITM_BOND,io=ESP_IO_CAP_OUT,key_size=16,keys=ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK;
    if(!check(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&auth,1),"AUTH_MODE") ||
       !check(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&io,1),"DISPLAY_PASSKEY") ||
       !check(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&key_size,1),"KEY_SIZE") ||
       !check(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,&keys,1),"INIT_KEYS") ||
       !check(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,&keys,1),"RESPONSE_KEYS"))return false;
    esp_hidh_config_t hid{};hid.callback=hid_callback;hid.event_stack_size=4096;
    if(!check(esp_hidh_init(&hid),"HID_HOST"))return false;
    if(xTaskCreate(worker,"cm_hid_open",6144,nullptr,5,&worker_handle)!=pdPASS){fail(ESP_ERR_NO_MEM);return false;}
    nvs_handle_t handle=0;const esp_err_t opened=nvs_open("cm_keyboard",NVS_READONLY,&handle);
    if(opened==ESP_OK) {
        size_t size=sizeof(Peer);const esp_err_t result=nvs_get_blob(handle,"peer",&saved_peer,&size);nvs_close(handle);
        if(result==ESP_OK && size==sizeof(Peer) && saved_peer.version==1 && saved_peer.type<=BLE_ADDR_TYPE_RANDOM && has_bond(saved_peer))view.saved=true;
        else if(result!=ESP_ERR_NVS_NOT_FOUND){fail(ESP_ERR_INVALID_STATE);return false;}
    } else if(opened!=ESP_ERR_NVS_NOT_FOUND){fail(opened);return false;}
    initialized=true;view.state=KeyboardState::disconnected;retry_due=now+1000;return true;
}
void keyboard_scan(uint64_t now) {
    if(!initialized || device || forget_pending || view.state==KeyboardState::searching || view.state==KeyboardState::pairing || view.saved)return;
    candidate={};view.candidates=0;view.error=0;manual_off=true;
    view.state=KeyboardState::searching;deadline=now+10000;
    esp_ble_scan_params_t scan{};scan.scan_type=BLE_SCAN_TYPE_ACTIVE;scan.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    scan.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;scan.scan_interval=0x50;scan.scan_window=0x30;scan.scan_duplicate=BLE_SCAN_DUPLICATE_ENABLE;
    check(esp_ble_gap_set_scan_params(&scan),"SCAN_PARAMS");
}
void keyboard_connect(uint64_t now) {
    if(!initialized || device || forget_pending || view.state==KeyboardState::pairing || view.state==KeyboardState::searching)return;
    if(view.saved)begin_open(saved_peer,now,true);
    else if(view.state==KeyboardState::found && view.candidates==1)begin_open(candidate,now,false);
}
void keyboard_disconnect() {
    if(view.state==KeyboardState::pairing || view.state==KeyboardState::searching)return;
    manual_off=true;decoder.reset();set_permission(nullptr);
    if(device)check(esp_hidh_dev_close(device),"DISCONNECT");
}
void keyboard_forget() {
    if(!initialized || device || forget_pending || view.state==KeyboardState::pairing || view.state==KeyboardState::searching || !view.saved)return;
    manual_off=true;set_permission(nullptr);decoder.reset();
    forget_pending=check(esp_ble_remove_bond_device(saved_peer.address),"FORGET_SELECTED_BOND");
}
void keyboard_confirm(bool accept) {
    if(!view.numeric_confirmation)return;
    check(esp_ble_confirm_reply(permitted.address,accept),"CONFIRM_PAIRING");view.numeric_confirmation=false;
}
void keyboard_poll(uint64_t now,KeySink sink,void* context) {
#ifdef CONFIG_CM_M4_TERMINAL
    decoder.local_zoom(true);
#endif

    if(!initialized)return;
    portENTER_CRITICAL(&lock);const bool overflow=dropped;dropped=false;
    const bool busy=worker_busy;view.worker_stack_min=worker_low;view.callback_stack_min=callback_low;portEXIT_CRITICAL(&lock);
    if(overflow){fail(ESP_ERR_NO_MEM);return;} // Lost releases cannot leave repeats active.
    Event e{};
    while(xQueueReceive(events,&e,0)==pdTRUE) {
        if(e.kind==Kind::params && view.state==KeyboardState::searching) {
            if(e.status)fail(e.status);else check(esp_ble_gap_start_scanning(6),"SCAN");
        } else if(e.kind==Kind::match && view.state==KeyboardState::searching) {
            if(!view.candidates){std::memcpy(candidate.address,e.address,6);candidate.type=e.addr_type;view.candidates=1;}
            else if(std::memcmp(candidate.address,e.address,6))view.candidates=2;
        } else if(e.kind==Kind::scan_done && view.state==KeyboardState::searching) {
            if(e.status)fail(e.status);
            else {view.state=view.candidates==1?KeyboardState::found:KeyboardState::disconnected;
                std::printf("M2 keyboard scan_complete matches=%u ambiguity_blocks_pairing=%d\n",view.candidates,view.candidates>1);}
        } else if(e.kind==Kind::opened) {
            const uint8_t* addr=esp_hidh_dev_bda_get(e.dev);
            if(e.status || !addr || !selected(addr)){if(e.dev)esp_hidh_dev_close(e.dev);}
            else if(!inspect_reports(e.dev)){device=e.dev;fail(ESP_ERR_NOT_SUPPORTED);}
            else {
                device=e.dev;
                // Explicitly require encryption even if the peer's descriptor
                // characteristics can be read without triggering SMP.
                if(check(esp_ble_set_encryption(permitted.address,ESP_BLE_SEC_ENCRYPT_MITM),"ENCRYPT_LINK"))connection_ready();
            }
        } else if(e.kind==Kind::closed) {
            if(device==e.dev)device=nullptr;
            closing=e.dev;allowed_count=0;auth_ok=false;view.bonded=false;
            view.show_passkey=false;view.numeric_confirmation=false;decoder.reset();
            if(view.state!=KeyboardState::error)view.state=KeyboardState::disconnected;
            retry_due=now+2000;
            std::printf("M2 keyboard closed reason=%d held_keys_reset=1\n",e.status);
        } else if(e.kind==Kind::auth && view.state==KeyboardState::pairing) {
            std::printf("M2 keyboard auth result=%d mode=%u passkey=REDACTED\n",e.status,static_cast<unsigned>(e.number));
            if(e.status || !(e.number&ESP_LE_AUTH_BOND))fail(e.status?e.status:ESP_ERR_INVALID_STATE);
            else {auth_ok=true;view.auth_mode=e.number;view.show_passkey=false;connection_ready();}
        } else if((e.kind==Kind::passkey || e.kind==Kind::numeric) && view.state==KeyboardState::pairing && selected(e.address)) {
            view.passkey=e.number;view.show_passkey=true;view.numeric_confirmation=e.kind==Kind::numeric;
            std::printf("M2 keyboard pairing_prompt=%s value=REDACTED\n",e.kind==Kind::numeric?"CONFIRM":"TYPE_ON_KEYBOARD");
        } else if(e.kind==Kind::open_done) {
            if(e.status && view.state==KeyboardState::pairing) {
                view.state=!view.saved && view.candidates==1?KeyboardState::found:KeyboardState::disconnected;
                view.error=e.status;decoder.reset();
                retry_due=now+(retries<4?(2000u<<retries):30000u);++retries;
            }
        } else if(e.kind==Kind::input && device==e.dev && view.state==KeyboardState::connected) {
            bool accepted=false;for(unsigned i=0;i<allowed_count;++i)accepted=accepted || (allowed[i].map==e.map && allowed[i].id==e.report_id);
            if(accepted) {
                ++view.reports;
                if(e.length!=8 || !decoder.report(e.data,e.length,now,sink,context)){++view.rejected;decoder.reset();}
            }
        } else if(e.kind==Kind::forget_done && forget_pending) {
            forget_pending=false;
            if(e.status)fail(e.status);
            else {
                nvs_handle_t handle=0;esp_err_t result=nvs_open("cm_keyboard",NVS_READWRITE,&handle);
                if(result==ESP_OK){result=nvs_erase_key(handle,"peer");if(result==ESP_OK)result=nvs_commit(handle);nvs_close(handle);}
                if(check(result,"FORGET_PEER_METADATA")){wipe(&saved_peer,sizeof(saved_peer));view.saved=false;view.candidates=0;view.state=KeyboardState::disconnected;}
            }
        }
        wipe(&e,sizeof(e));
    }
    if(closing && !busy){esp_hidh_dev_free(closing);closing=nullptr;}
    if(view.state==KeyboardState::connected)decoder.tick(now,sink,context);
    if((view.state==KeyboardState::searching || view.state==KeyboardState::pairing) && now>=deadline)fail(ESP_ERR_TIMEOUT);
    if(view.state==KeyboardState::disconnected && view.saved && !manual_off && !busy && !closing && now>=retry_due)
        begin_open(saved_peer,now,true);
}
KeyboardSnapshot keyboard_snapshot(){return view;}
const char* keyboard_state_name(KeyboardState state) {
    switch(state) {
        case KeyboardState::off:return "OFF";case KeyboardState::starting:return "STARTING";
        case KeyboardState::disconnected:return "DISCONNECTED";case KeyboardState::searching:return "SEARCHING";
        case KeyboardState::found:return "B1 FOUND";case KeyboardState::pairing:return "PAIRING";
        case KeyboardState::connected:return "CONNECTED";case KeyboardState::error:return "ERROR";
    }return "ERROR";
}
}
