#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d -t cyd-sd-tests)"
trap 'rm -rf "$test_dir"' EXIT
crypto=-DCYD_NATIVE_APPLE
if [[ "$(uname -s)" != Darwin ]]; then crypto=-lcrypto; fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -DCYD_LIBRARY_DIR='"firmware"' \
 -I "$project_root/tests/fakes" -I "$project_root/firmware/main" \
 "$project_root/tests/test_sd_installer.cpp" "$project_root/firmware/main/firmware_image.cpp" "$project_root/firmware/main/sd_installer.cpp" \
 "$crypto" -o "$test_dir/check"
cd "$test_dir"
"$test_dir/check" "$1"
