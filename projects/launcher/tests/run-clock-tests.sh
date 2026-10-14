#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d -t cyd-clock-tests)"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/firmware/components/cyd_clock/include" \
 "$project/tests/test_clock.cpp" "$project/firmware/components/cyd_clock/clock_policy.cpp" -o "$test_dir/check"
"$test_dir/check"
