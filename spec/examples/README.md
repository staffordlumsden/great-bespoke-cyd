# Buildable new-app example

`minimal-app.cpp` is the source used by `../../tools/new_app.py`. It demonstrates
PixelFonts, dark/mint theme persistence, healthy first-boot acknowledgement,
shared clock initialization, touch debounce and safe return to Console. It is
an example, not an accepted user app. It uses no Bluetooth or Wi-Fi.

Generate to a new project directory, build, inspect the package, then add domain
features in small changes. The generator uses existing build defaults and
Console bridge code; it refuses to overwrite existing output. All generated
root paths are relative so the same source works in a portable snapshot.

Keep display callbacks bounded and convert RGB565 byte order as the shared HAL
expects. Persist only owned NVS keys; never erase NVS to handle initialization
failure. Before Console return, checkpoint/close data and unmount SD. For an app
that does not require SD, define its health gate accordingly; this example
requires display, touch, FAT32 and a valid resident Console.

The example prints no network data or note content. The helper does not install
or flash. After extensions, meaningful native tests and user observations must
establish acceptance; a successful example build proves compilation only.
