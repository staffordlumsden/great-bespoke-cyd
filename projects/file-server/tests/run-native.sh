#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$project/artifacts/native"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I "$project/firmware/main" -I "$project/../launcher/firmware/main" \
 "$project/tests/test_policy_ui.cpp" "$project/firmware/main/file_policy.cpp" -o "$project/artifacts/native/check"
"$project/artifacts/native/check" "$project/artifacts/native"
