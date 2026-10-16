# Great Bespoke CYD firmware specification

## Product intent

A portable, touch-first toolkit on a low-cost CYD: a resident Console and
small separate SD-installed firmware apps. The Console stays simple, apps
share readable PixelFonts and themes, and data/configuration persist on SD.
Humans choose outcomes and judge physical readability; agents implement,
test, package and leave reproducible evidence. This public edition includes
Console, File Server, Terminal Micro, Quicknote Micro, Weather Micro, LED Micro
and the early-access Usage app.

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
- An app confirms pending OTA only after its critical startup checks and first
  successfully rendered frame pass. Keep large UI/snapshot buffers off task
  stacks; enforce compiler frame limits and measure runtime stack margins.
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

## Usage

Multi-provider dashboard: Codex reuses the host CLI login over a read-only TLS
bridge; optional OpenRouter shows per-key spending; custom HTTPS feeds use a
small normalized schema. Keep OAuth on the host, not the device. Providers have
independent caches and retries. Ollama uses the official fixed-host bearer API: included or legacy
allowances, reset countdowns, purchased credits and hourly 24H requests. Convert
API remaining percentages to used percentages. Show unavailable cost/token
metrics as unknown; never invent plan or model statistics. Private key comes
from SD `/private/usage.json` (legacy `/private/ollama.json` fallback), not the binary, cookies or NVS. Require valid
time and verified HTTPS; disable redirects, bound JSON/transport work, respect
rate limits and mark retained failed/aged data stale. Reuse read-only SD Wi-Fi
profiles. Keep network I/O off the touch task and provide Console return.
Its basic SD install/Codex-Ollama switching is user-confirmed; extended checks
remain pending.

## LED Micro

Offline active-low RGB control: R4/G16/B17, inverted hardware PWM, bounded
brightness/RGB channels, monotonic solid/flash/alternating patterns and white
flashlight. Power starts OFF and Console return stops PWM/drives outputs off.
Persist only the app-owned led_ui settings, never power state. Reuse PixelFonts,
34 px minimum controls and both themes; no radio initialization or SD config.
See the app guide and status index for functional acceptance and measurements
that were not taken. No other hardware peripherals are repurposed.

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
Publish one current package per app, SHA256SUMS and licence notices. Label candidate apps as early-access prereleases and list their pending physical checks; never silently replace an accepted image with an unaccepted rebuild.
A rebuilt candidate is not hardware accepted merely because its source matches
an accepted version. Keep known limitations and handoff state explicit.

## Weather Micro

Fixed-origin verified Open-Meteo HTTPS, valid time before TLS, a bounded worker
and strict JSON/units/null handling. Read existing SD networks without rewriting
credentials. Search/save at most six places in wx_ui; bind forecast caches to
place IDs and discard removed-place replies. Current conditions, next five hourly
point temperatures, 24-hour rain/wind/sun/moon panels and C/F. Daily high/low
is distinct from hourly point temperature. Mark stale/unknown values honestly;
moon illumination and daylight arc are schematic. Preserve app-owned settings,
rate limits, Console access and an 8 KiB main task with measured stack margins.
See the app guide for DST-offset limits and exact acceptance.

## Quicknote Micro

BLE B1 editor with adjustable PixelFonts, Ctrl+S/touch save, recoverable bounded
drafts and daily history. Save timestamped Markdown to
/files/QuickNote/Daily/YYYY-MM-DD.md; use Console clock and block saving if time
is invalid. File Server transfers saved notes. A failed append retains editor
text. Own only qn_ui settings; preserve shared BLE bond and other NVS namespaces.
Plaintext SD notes are private user data, never sample screenshots.
