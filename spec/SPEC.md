# Great Bespoke CYD firmware specification

## Product intent

A portable, touch-first toolkit on a low-cost CYD: a resident Console and
small separate SD-installed firmware apps. The Console stays simple, apps
share readable PixelFonts and themes, and data/configuration persist on SD.
Humans choose outcomes and judge physical readability; agents implement,
test, package and leave reproducible evidence. This public edition includes
Console, File Server and Terminal Micro only.

## Required architecture

- ESP-IDF v5.5.5 at the pinned commit in `reference.md`, target esp32, 4 MB,
  DIO 40 MHz, rollback enabled, no PSRAM assumption.
- One shared `partitions.csv`. Factory Console plus two OTA slots; do not
  alter layout as part of a routine app upgrade.
- `projects/launcher/firmware/components/cyd_hal` owns LCD/touch/SD hardware.
  `cyd_clock` owns retained time/preferences and time conversion. App-owned
  NVS namespaces prevent overwriting other apps or Bluetooth bonds.
- `common/terminal-core` provides bounded SSH/ANSI/Wi-Fi/keyboard code;
  `common/libssh2_esp` retains its own licence and bounded-memory changes.
- FAT32 SD `/firmware/<Project>-<version>.bin` with adjacent `.json` and
  `-LICENSE.txt`. Maximum app 0x1b0000 bytes. JSON and the appended ESP image
  SHA-256 must verify before selecting the installed slot. Hash checks
  detect corruption; packages are not cryptographically publisher-signed.
- An app confirms pending OTA only after its critical startup checks pass.
  Every app offers a visible Console return using the factory partition.
- Keep one `CYDFileServer` version containing `-clock` for Console's Wi-Fi
  clock helper. Ordinary apps rotate through the other slot.

## Console

PixelFonts title/version, clock, flat theme/radio/shutdown tiles, SD library,
capacity information. Install + run, Run last and progress/error feedback.
Wi-Fi and Bluetooth tiles are currently informational placeholders. Sleep
must wait for touch release, unmount SD, sleep display/backlight, hold GPIO21
low in deep sleep and wake through touch IRQ36. Recovery: hold touch during
power-on for at least five seconds to boot factory; keep NVS intact.

## File Server

Browser-based LAN SD upload/download, directory navigation/creation, transfer
progress/cancel and readable numeric code. Display address/port/code on device.
Setup hotspot permits new 2.4 GHz network provisioning without recompiling.
Dark and mint/blue light modes exist on device and browser. Expose only `/files` and `/firmware`; protect
configuration and key paths; reject traversal/NUL/control/invalid UTF-8 and
use bounded request buffers. Write uploads safely and handle cancellation;
report failure instead of an apparent successful partial file. HTTP on a
trusted LAN; access code is not TLS. No measured universal throughput promise.

## Terminal Micro

BLE HID keyboard (accepted Winmaxle B1), saved bond reconnect, pairing and
explicit forget; Wi-Fi saved profile controls; selectable SSH hosts with
pinned host-key verification before key authentication. SSH shell, bounded
ANSI screen/history and alternate screen, on-device font controls and keyboard
zoom, actual PTY resizing, host label in header, touch scrolling and Live.
Retain the accepted AES-CTR/MAC/packet chunk limits; test heap with BLE and
Wi-Fi active and high-output interactive apps. No full xterm or arbitrary
Unicode claim. Remote applications execute on the SSH server, not on CYD.

## UX requirements

Use shared PixelFonts with full licence; readable controls, stable pages,
release-based touch actions and a single source of draw/hit geometry.
No continuous repainting of unchanged menus. Show labels as well as colours;
red=off/error, amber=busy, green=ready. Keep inactive features visibly disabled.
Light apps use mint/blue rather than glaring white; see `design.md`.
No private addresses, chat contents or keys in public screenshots/logs.

## Release requirements

A source build must succeed from this public layout. Native tests cover bounds,
parser/paths/hash failures, themes/hit targets, stale rows and relevant clocks.
Physical tests cover actual install + run, NVS/bond retention, Console return,
Run last, recovery, readable display and the app's real network/keyboard use.
Label unperformed tests and separate renderer fixtures from device photographs.
Publish one latest accepted package per app, SHA256SUMS and licence notices.
A rebuilt candidate is not hardware accepted merely because its source matches
an accepted version. Keep known limitations and handoff state explicit.
