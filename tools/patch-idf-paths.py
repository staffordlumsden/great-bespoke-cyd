#!/usr/bin/env python3
"""Apply a bounded build-only workaround to the pinned ESP-IDF checkout.

ldgen.cmake's spaces2list splits absolute BT linker-fragment paths. Resolve the
fragment relative to its components/bt registration instead. No firmware code,
linker-fragment contents, compiler options or SDK commit pin are changed.
"""
from pathlib import Path
import sys

p = Path(__file__).resolve().parents[1] / '.tools/esp-idf/components/bt/controller/CMakeLists.txt'
original = 'list(APPEND bt_ctrl_ldscripts "${CMAKE_CURRENT_LIST_DIR}/linker_rw_bt_controller.lf")'
replacement = 'list(APPEND bt_ctrl_ldscripts "controller/linker_rw_bt_controller.lf")'
s = p.read_text()
if s.count(original) == 3 and replacement not in s:
    p.write_text(s.replace(original, replacement))
elif s.count(replacement) != 3 or original in s:
    sys.exit('Unexpected Bluetooth build file; refusing path workaround.')
