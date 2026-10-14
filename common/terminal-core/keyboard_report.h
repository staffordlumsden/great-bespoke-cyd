#pragma once
#include <cstddef>
#include <cstdint>
namespace cm {
// Validate a report-mode descriptor before handing its eight-byte payload to
// BootKeyboard. Report IDs are supplied separately by the BLE HID transport.
bool canonical_keyboard_report(const uint8_t* map, size_t size, uint8_t report_id);
}
