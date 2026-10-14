#pragma once
#include <cstdint>
namespace fsapp {
enum class NetState {starting,connecting,connected,setup,error};
struct NetworkSnapshot {NetState state=NetState::starting;char ip[16]{},ssid[33]{},setup_ssid[33]{};bool setup=false;int rssi=0,error=0;};
struct ScanEntry {char ssid[33]{};int rssi=0;unsigned auth=0;};
bool network_start(const char* setup_password);
void network_poll(uint64_t now);
NetworkSnapshot network_snapshot();
bool network_setup();
bool network_retry();
bool network_scan();
unsigned network_scan_results(ScanEntry* out,unsigned capacity,bool& scanning);
bool network_join(const char* ssid,const char* password);
}
