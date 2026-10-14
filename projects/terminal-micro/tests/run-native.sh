#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/tests/fakes" -I "$project/firmware/main" \
 -I "$root/common/terminal-core" \
 -I "$root/projects/launcher/firmware/main" -I "$root/projects/launcher/firmware/components/cyd_hal/include" \
 "$project/tests/test_terminal.cpp" "$project/firmware/main/terminal_ui.cpp" \
 "$project/firmware/main/terminal_renderer.cpp" "$root/common/terminal-core/terminal_parser.cpp" \
 -o "$output/check"
"$output/check" "$output"
