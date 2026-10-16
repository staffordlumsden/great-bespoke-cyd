# Hardware, build and layout reference

## Supported profile

Tested: dual-UART-USB ESP32-2432S028R, ESP32-D0WD-V3 rev3.1, 4 MB flash,
40 MHz crystal, 320×240. Exact LCD silicon/PCB revision and PSRAM identity
remain unverified. Software uses `esp_lcd_new_panel_st7789`, BGR, swapped axes,
both mirror bits true, inversion off. CYD clones need independent verification.
The narrow inactive glass margin is not a framebuffer offset.

| Interface | Pins / details |
|---|---|
| LCD SPI | MOSI13, MISO12, CLK14, CS15, DC2, backlight21 |
| Resistive touch software SPI | MOSI32, MISO39, CLK25, CS33, IRQ36 |
| SD SPI3 | MOSI23, MISO19, CLK18, CS5; FAT32; `/sdcard` |
| USB | UART bridges; no native USB mass storage |

| Partition | Offset | Size |
|---|---:|---:|
| nvs | 0x9000 | 0x6000 |
| phy_init | 0xf000 | 0x1000 |
| factory Console | 0x10000 | 0x80000 |
| app0 | 0x90000 | 0x1b0000 |
| app1 | 0x240000 | 0x1b0000 |
| otadata | 0x3f0000 | 0x2000 |

See root `partitions.csv`; bootloader at 0x1000, partition table at 0x8000.
Never erase NVS to troubleshoot; retain unmodified backup before migration.

## Pinned dependencies

ESP-IDF v5.5.5 / `b774170ff46c393eeb5e495ea37936038d3f4f4f`.
PixelFonts `344a6cfcd660b8546258485befb7576a47790d7d`.
Port `198771df0a67156a921a0acf6552d431462a0fdc`, libssh2
`455f062236fdd5deba9abb909dbe3b86463a23d8` (1.11.1).
See `../THIRD_PARTY.md`. SDK/tool packages are installed under ignored `.tools/`.
The bounded BT linker-fragment patch permits checkouts containing spaces.
Python 3.10–3.13 is required for the pinned setup; use a base interpreter.

## Entry points, from checkout root

```sh
bash tools/setup-idf.sh
bash projects/launcher/firmware/tools/build.sh build
bash projects/file-server/firmware/tools/build.sh build
bash projects/terminal-micro/firmware/tools/build.sh build
bash tools/test.sh
python3 tools/new_app.py --project HelloCYD --output projects/hello-cyd
```

Default Terminal grid: 40×15 with PixelFonts 11x5 thin in 8×14 cells;
small: 53×21 (7x5 thin, 6×10); large: 32×13 (13x7 semibold, 10×16).
App slot maximum: 1,769,472 bytes; factory maximum: 524,288 bytes.
Serial port names vary; discover them every session, never bake one into code.

## Usage candidate

Project `projects/usage`, image `Usage`, version 0.2.0. Prepared image
1,050,400 bytes. Config `/private/usage.json`, optional bridge trust
`/private/usage-ca.pem`; legacy Ollama config fallback. `ou_ui` theme namespace
retained. Four sources; fixed Ollama/OpenRouter APIs or authenticated HTTPS feed.
Codex helper reads existing login through app-server, never copies OAuth.
Physical acceptance remains pending.

```sh
bash projects/usage/tests/run-native.sh
bash projects/usage/firmware/tools/build.sh build
```

## RGB LED app

LED Micro 0.1.0: 306,240 bytes, SHA-256
`41af56faff22d0ad2625f06fa82a0abfb3c5b71345a276544dd405a7efb05512`.
GPIO4 red, GPIO16 green, GPIO17 blue, active low; low-speed LEDC timer0,
channels0–2, inverted 4 kHz/12-bit PWM. User confirmed colour/brightness/patterns
and Console/Run last on 16 October 2026; no measured PWM frequency/current claim.
The pin map is from the [primary reference](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md#rgb-led).

## Weather and Quicknote

WeatherMicro 0.1.1-stack: 1,100,288 bytes; fixed verified Open-Meteo forecast/
geocoding origins, up to six saved places, wx_ui preferences. Main and HTTPS
worker stacks are 8 KiB. Compiler frame limit 2 KiB; bounded runtime search
capture minimum main/worker stack 5,064/4,444 bytes. First frame precedes boot ack.
QuicknoteMicro 0.1.0: 807,408 bytes; qn_ui preferences, shared BLE bond/clock,
/files/QuickNote daily Markdown, hidden .draft recovery. No Wi-Fi required.

```sh
bash projects/weather-micro/tests/run-native.sh
bash projects/weather-micro/firmware/tools/build.sh build
bash projects/quicknote-micro/tests/run-native.sh
bash projects/quicknote-micro/firmware/tools/build.sh build
```
