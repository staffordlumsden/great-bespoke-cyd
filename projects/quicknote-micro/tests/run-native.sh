#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/quicknote-native.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I "$project/firmware/main" -I "$root/projects/launcher/firmware/main" \
  -I "$root/common/terminal-core" \
  "$project/tests/test_quicknote.cpp" "$project/firmware/main/note_model.cpp" \
  "$project/firmware/main/quicknote_ui.cpp" \
  -o "$temporary/check"
"$temporary/check" "$output"
