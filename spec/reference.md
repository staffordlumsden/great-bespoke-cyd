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

## Ollama Cloud Usage candidate

Project `projects/ollama-cloud-usage`, ESP image name `OllamaUsage`, version
0.1.0. Prepared app image 1,032,400 bytes, original-ESP32 OTA slot only.
Private SD config `/private/ollama.json`; app-owned NVS `ou_ui` stores theme,
never credentials. Wi-Fi uses `common/terminal-core`, with read-only SD profiles.
Official fixed HTTPS balance/history endpoints, 60s healthy polling, 24 KiB
response cap, five-minute display sleep. Physical acceptance pending.

```sh
bash projects/ollama-cloud-usage/tests/run-native.sh
bash projects/ollama-cloud-usage/firmware/tools/build.sh build
```
