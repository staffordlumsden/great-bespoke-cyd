#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d -t cyd-ui-tests)"
trap 'rm -rf "$test_dir"' EXIT
mkdir -p "$1"
crypto=-DCYD_NATIVE_APPLE
if [[ "$(uname -s)" != Darwin ]]; then crypto=-lcrypto; fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project_root/tests/fakes" -I "$project_root/firmware/main" \
 -I "$project_root/firmware/components/cyd_hal/include" \
 -I "$project_root/firmware/components/cyd_clock/include" \
 "$project_root/tests/test_loader_ui.cpp" "$project_root/firmware/main/firmware_image.cpp" "$project_root/firmware/components/cyd_clock/clock_policy.cpp" "$crypto" -o "$test_dir/check"
"$test_dir/check" "$1"
