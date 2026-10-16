#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/firmware/main" -I "$root/projects/launcher/firmware/main" \
 "$project/tests/test_led.cpp" "$project/firmware/main/led_model.cpp" "$project/firmware/main/ui.cpp" -o "$output/check"
"$output/check" "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/tests/fakes" -I "$project/firmware/main" \
 "$project/tests/test_output.cpp" "$project/firmware/main/led_model.cpp" "$project/firmware/main/led_output.cpp" -o "$output/check-output"
"$output/check-output"
