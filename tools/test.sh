#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
bash projects/launcher/tests/run-ui-tests.sh projects/launcher/artifacts/native
bash projects/launcher/tests/run-clock-tests.sh
bash projects/launcher/tests/run-sd-tests.sh "$root/distribution/sd/firmware/TerminalMicro-0.2.3-headroom.bin"
bash projects/file-server/tests/run-native.sh
bash projects/terminal-micro/tests/run-native.sh
bash projects/usage/tests/run-native.sh
python3 tools/validate_public.py
