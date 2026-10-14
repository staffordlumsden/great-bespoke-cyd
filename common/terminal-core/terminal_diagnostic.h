#pragma once
namespace cm {
// Public fixtures executed on isolated SSH channels, never keyboard input.
constexpr char terminal_colour_command[] = R"CM(printf '\033[0m\033[2J\033[HCOLOUR TEST\r\n\r\n\033[38;5;196;48;5;196m  \033[49m RED\r\n\033[38;5;46;48;5;46m  \033[49m GREEN\r\n\033[38;5;226;48;5;226m  \033[49m YELLOW\r\n\033[38;5;51;48;5;51m  \033[49m CYAN\r\n\r\n\033[97mTEST COMPLETE\r\n\033[92mReconnect SSH to return.\r\n')CM";
constexpr char terminal_scroll_command[] =
    R"CM(printf '\033[0m\033[2J\033[HSCROLL TEST\r\n'; i=1; while [ "$i" -le 80 ]; do printf 'LINE %02d\r\n' "$i"; i=$((i+1)); sleep 0.15; done)CM";
constexpr char terminal_colour_fixture[] = "\033[0m\033[2J\033[HCOLOUR TEST\r\n\r\n\033[38;5;196;48;5;196m  \033[49m RED\r\n\033[38;5;46;48;5;46m  \033[49m GREEN\r\n\033[38;5;226;48;5;226m  \033[49m YELLOW\r\n\033[38;5;51;48;5;51m  \033[49m CYAN\r\n\r\n\033[97mTEST COMPLETE\r\n\033[92mReconnect SSH to return.\r\n";
}
