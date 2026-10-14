#pragma once
#include <cstdint>
namespace fsapp {
enum class Phase {idle,upload,download,complete,cancelled,error};
struct TransferSnapshot {Phase phase=Phase::idle;bool busy=false;char id[25]{},name[129]{},message[96]{};uint64_t done=0,total=0,started_ms=0,ended_ms=0;};
constexpr unsigned server_port=8080;
bool server_start();
void server_stop();
void server_cancel(const char* id=nullptr);
TransferSnapshot transfer_snapshot();
const char* access_code();
void server_storage(uint64_t& total,uint64_t& free);
}
