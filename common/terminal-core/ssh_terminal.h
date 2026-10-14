#pragma once
#include "ssh_client.h"
#include <libssh2.h>
namespace cm {
int ssh_terminal_run(LIBSSH2_SESSION*,int,const HostProfile&,void(*)(SshState,int,bool),bool(*)(),unsigned diagnostic_mode);
bool ssh_terminal_input(const char*,size_t);
bool ssh_terminal_diagnostic(bool scroll);
bool ssh_terminal_cleanup(LIBSSH2_SESSION*,int);
}
