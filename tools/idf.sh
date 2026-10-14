#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
firmware="${1:?Provide the firmware project directory}"; shift
sdk="$root/.tools/esp-idf"
export IDF_PATH="$sdk" IDF_TOOLS_PATH="$root/.tools/espressif"
if [[ ! -f "$sdk/export.sh" ]]; then echo 'Run bash tools/setup-idf.sh first.' >&2; exit 1; fi
[[ "$(git -C "$sdk" rev-parse HEAD)" == b774170ff46c393eeb5e495ea37936038d3f4f4f ]] || { echo 'SDK pin mismatch' >&2; exit 1; }
for arg in "$@"; do
 case "$arg" in flash|app-flash|erase-flash|erase_flash) echo 'Use the documented backup-first install process; generic flashing is disabled.' >&2; exit 1;; esac
done
python3 "$root/tools/patch-idf-paths.py"
for env_dir in "$IDF_TOOLS_PATH"/python_env/idf5.5_py*_env; do
 [[ ! -d "$env_dir/bin" ]] || export PATH="$env_dir/bin:$PATH"
done
source "$sdk/export.sh"
cd "$firmware"
exec idf.py "$@"
