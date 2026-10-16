#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/weather-native.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT
"${CC:-cc}" -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined -g -c "$project/tests/third_party/cjson/cJSON.c" -o "$temporary/json.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/firmware/main" -I "$root/projects/launcher/firmware/main" -I "$project/tests/third_party/cjson" \
 "$project/tests/test_weather.cpp" "$project/firmware/main/weather_model.cpp" "$project/firmware/main/ui.cpp" "$temporary/json.o" -o "$output/check"
"$output/check" "$project/tests/fixtures" "$output"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/tests/fakes" -I "$project/firmware/main" -I "$project/tests/third_party/cjson" \
 "$project/tests/test_http.cpp" "$project/firmware/main/weather_model.cpp" "$temporary/json.o" -o "$output/check-http"
"$output/check-http" "$project/tests/fixtures"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I "$project/firmware/main" -I "$root/common/terminal-core" -I "$project/tests/third_party/cjson" \
 "$project/tests/test_network.cpp" "$project/firmware/main/network_config.cpp" "$temporary/json.o" -o "$output/check-network"
"$output/check-network" "$temporary/card"
