#pragma once
#include "config_store.h"
namespace cm {
enum class SshState {off,idle,resolving,connecting,handshake,verifying,loading_key,authenticating,authenticated,pty,shell,terminal,trust_blocked,disconnected,error};
enum class TrustProbe {normal,changed_pin,missing_pin,handshake_timeout};
struct SshSnapshot {SshState state=SshState::off;int error=0;bool authentication_started=false;unsigned worker_stack_min=0;};
bool ssh_start(const AppConfig& config);
bool ssh_select_host(unsigned index);
unsigned ssh_selected_host();
void ssh_connect(TrustProbe probe);
bool ssh_shell();
void ssh_disconnect();
bool ssh_active();
bool ssh_input(const char*,size_t);
bool ssh_diagnostic(bool scroll=false);
void ssh_poll();
SshSnapshot ssh_snapshot();
const char* ssh_state_name(SshState state);
}
