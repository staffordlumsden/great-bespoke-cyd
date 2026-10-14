#!/usr/bin/env bash
set -euo pipefail
firmware="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$firmware/../../.." && pwd)"
python3 "$firmware/../tools/generate_web_assets.py"
exec bash "$root/tools/idf.sh" "$firmware" "$@"
