#!/usr/bin/env bash
set -euo pipefail

shared_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_path="$shared_root/.tools/esp-idf"
expected_commit="b774170ff46c393eeb5e495ea37936038d3f4f4f"
export IDF_TOOLS_PATH="$shared_root/.tools/espressif"

command -v git >/dev/null
cm_candidate="$(command -v python3.13 || command -v python3)"
cm_python="${CM_PYTHON:-$("$cm_candidate" -c 'import sys; print(sys._base_executable)')}"
"$cm_python" -c 'import sys; assert (3, 10) <= sys.version_info[:2] <= (3, 13), "Use Python 3.10-3.13 for this pinned setup"; assert sys.prefix == sys.base_prefix, "Select a base interpreter with CM_PYTHON, not an active virtual environment"'
mkdir -p "$shared_root/.tools"

if [[ ! -d "$sdk_path" ]]; then
    git clone --depth 1 --branch v5.5.5 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$sdk_path"
fi
if [[ "$(git -C "$sdk_path" rev-parse HEAD)" != "$expected_commit" ]]; then
    printf '%s\n' 'SDK commit mismatch; refusing to modify the existing SDK.' >&2
    exit 1
fi

# Keep compiler packages and the Python virtual environment local to this repo.
git -C "$sdk_path" submodule update --init --recursive --depth 1
"$cm_python" "$sdk_path/tools/idf_tools.py" install --targets=esp32
"$cm_python" "$sdk_path/tools/idf_tools.py" install-python-env --features=core
export IDF_PATH="$sdk_path"
"$cm_python" "$shared_root/tools/patch-idf-paths.py"
source "$sdk_path/export.sh"
# These isolated tools avoid dependence on the system CMake major version.
python -m pip install 'cmake==3.31.6' 'ninja==1.11.1.4'
printf '%s\n' 'Pinned SDK ready. Run: bash projects/launcher/firmware/tools/build.sh build'
