# Install and recover

These images target the profile in [reference](../spec/reference.md), not every
board sold as CYD. Wrong pins/display/flash geometry need a separate port.
The Console is custom ESP-IDF firmware, independent of M5Launcher.

## Existing Great Bespoke CYD Console

1. Download the release bundle and verify `SHA256SUMS.txt` with
   `shasum -a 256 -c SHA256SUMS.txt` (or `sha256sum -c` on Linux), from the
   extracted `distribution` folder. Keep matching `.bin`/`.json`/licences.
2. Power off and put the FAT32 microSD in your reader. Copy the contents of
   `distribution/sd/` onto the card, preserving its existing files. This places
   the latest File Server and Terminal Micro under `/firmware`.
3. Safely eject, return the card, boot Console. Tap **Load from SD Card**,
   select File Server, then **Install + run**. Return via **Console**, install
   Terminal Micro in the same way. Install File Server first to retain clock
   helper support. Do not format the SD card or remove private app data.
4. **Run last** opens the last successful installed package. Verify Console
   return, Run last, retained keyboard bond and the app's actual operation.

## First installation / another flash layout

A new device needs a matching bootloader, table and factory Console. The app
images alone cannot bootstrap it. This changes the device's existing firmware;
read this entire section before writing. Use the pinned SDK's esptool, an
explicit serial port, and one serial owner. No `erase_flash` is needed.

```sh
bash tools/setup-idf.sh
# Activate tools in your own shell; repo root must be the current directory.
export IDF_TOOLS_PATH="$PWD/.tools/espressif"
. .tools/esp-idf/export.sh
# Replace PORT with the port you discovered; never guess another device.
python -m esptool --chip esp32 --port PORT chip_id
python -m esptool --chip esp32 --port PORT flash_id
# Make this private backup OUTSIDE the public checkout, before any write.
python -m esptool --chip esp32 --port PORT read_flash 0 0x400000 /PRIVATE/PATH/cyd-before.bin
```

Confirm original ESP32 and 4 MB flash. Inspect/retain the old partition table
and full backup. A differently located old NVS will not automatically migrate;
back up and plan that separately. The commands below reserve 0x9000–0xefff for
NVS and do not write that range, but changing layouts can orphan previous data.
Do not perform migration on a device whose identity/layout is uncertain.
After verifying release hashes, bootstrap only these four images:

```sh
python -m esptool --chip esp32 --port PORT write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB   0x1000 distribution/console/bootloader.bin   0x8000 distribution/console/partition-table.bin   0x10000 distribution/console/CYDConsole-0.3.3-sleep.bin   0x3f0000 distribution/console/ota_data_initial.bin
python -m esptool --chip esp32 --port PORT verify_flash   0x1000 distribution/console/bootloader.bin   0x8000 distribution/console/partition-table.bin   0x10000 distribution/console/CYDConsole-0.3.3-sleep.bin   0x3f0000 distribution/console/ota_data_initial.bin
```

On Windows use your serial port and shell's environment syntax. This release
has been source-built on macOS; Windows setup is not physically accepted.
Check display/touch/SD before proceeding to the SD app instructions above.

## Updating resident Console on this exact layout

Back up factory and verify that the existing table matches `partitions.csv`.
Write and verify **only** `CYDConsole-0.3.3-sleep.bin` at **0x10000** using the
explicit port. Do not rewrite OTA data or the bootloader during a routine
factory update. Generic build wrappers deliberately refuse `flash`.

## Recovery / sleep

Hold the touchscreen while applying power for at least five seconds, then
release: the bootloader selects factory Console, retaining NVS and SD data.
Every app also offers Console return. Console Shutdown is ESP32 deep sleep;
tap to wake. It cannot cut the USB supply, so a board power LED may stay on.
Keep power stable throughout installation. Hash checks detect corruption,
not a malicious unsigned package: install firmware only from trusted sources.
