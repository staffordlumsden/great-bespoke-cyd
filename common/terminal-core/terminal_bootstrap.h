#pragma once
#include <cstddef>
#include <cstring>

namespace cm {
// For an isolated SSH exec request on a newly allocated, verified PTY.
// A login child supplies the host PATH; a clean shell remains after startup exits.
inline size_t terminal_startup_command(char* output, size_t capacity,
                                      const char* alias, const char* command) {
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!alias || !command) return 0;
    const size_t alias_size = std::strlen(alias), command_size = std::strlen(command);
    if (!alias_size || alias_size > 12 || !command_size || command_size > 256) return 0;
    for (size_t i = 0; i < alias_size; ++i) {
        const unsigned char c = alias[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    for (size_t i = 0; i < command_size; ++i)
        if (static_cast<unsigned char>(command[i]) < 32 || command[i] == 127) return 0;
    size_t used = 0;
    bool valid = true;
    const auto append = [&](const char* text) {
        const size_t count = std::strlen(text);
        if (!valid || count >= capacity - used) { valid = false; return; }
        std::memcpy(output + used, text, count); used += count; output[used] = 0;
    };
    const auto quoted = [&](const char* text) {
        append("'");
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
            if (*p == '\'') append("'\\''");
            else { const char byte[] = {static_cast<char>(*p), 0}; append(byte); }
        }
        append("'");
    };
    append("export CM_MICRO_STARTUP="); quoted(command);
    append(" CM_MICRO_ALIAS="); quoted(alias);
    append("; exec \"${SHELL:-/bin/sh}\" -lc ");
    quoted(R"CM(export COLORTERM=truecolor FORCE_COLOR=3 OPENCLAW_THEME=dark; printf '\033[2J\033[H'; eval "$CM_MICRO_STARTUP"; cm_micro_rc=$?; if [ "$cm_micro_rc" -ne 0 ]; then printf '\033[93m\r\nCM STARTUP EXIT %s\033[0m\r\n' "$cm_micro_rc"; fi; cm_micro_alias=$CM_MICRO_ALIAS; unset CM_MICRO_STARTUP CM_MICRO_ALIAS; case "${SHELL##*/}" in zsh) exec env PS1="$(printf '%%{\033[97m%%}%s> %%{\033[92m%%}' "$cm_micro_alias")" RPROMPT= RPS1= "$SHELL" -f;; bash) exec env PS1='\[\e[97m\]'"$cm_micro_alias"'> \[\e[92m\]' "$SHELL" --noprofile --norc;; *) PS1="$(printf '\033[97m%s> \033[92m' "$cm_micro_alias")"; export PS1; exec "${SHELL:-/bin/sh}";; esac)CM");
    if (!valid) { output[0] = 0; return 0; }
    return used;
}
}
