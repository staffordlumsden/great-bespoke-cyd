# Terminal Micro

Accepted prebuilt release: **0.2.3-headroom**. Rebuilt sources require new physical
acceptance. Screenshots are actual firmware-renderer/browser captures with
public fixture data, not photographs or live private sessions.

A standalone, host-key-verified SSH terminal with PixelFonts and a BLE keyboard.
No remote application is launched automatically. Configure private SD profiles,
connect Wi-Fi, then Keyboard → Scan B1 → Pair (or wake a saved B1). Select a
host and Open terminal. Type the displayed passkey on the keyboard if prompted.

![Terminal lobby renderer](../../site/assets/screenshots/terminal-home-dark.png)
![Terminal renderer](../../site/assets/screenshots/terminal-session.png)

Tap the terminal header for Font −/+, Live output, Disconnect, Resume and Console.
Ctrl+Equals/Plus and Ctrl+Minus zoom and resize the actual PTY. Swipe vertically
or Shift+PageUp/Down for bounded main-screen history. Alternate-screen apps own
their history. Disconnect before changing hosts. Forget requires confirmation
and is available after disconnect. A saved Winmaxle B1 reconnects on wake.

Default 40×15 grid; small 53×21; large 32×13. Limited Unicode replacements and
ANSI support: no full xterm claim. Remote TUIs execute on the server. Combined
BLE/Wi-Fi/SSH heap is constrained; retain the app's Wi-Fi buffer sizing and
libssh2 staging limits. Never disable host pinning to fix an SSH error.

## Build and test

From repository root:

```sh
bash tools/setup-idf.sh
bash projects/terminal-micro/firmware/tools/build.sh build
bash tools/test.sh
```

Read [installation](../../docs/install.md), [private configuration](../../docs/configuration.md),
[release status](../../docs/status.md) and [third-party rights](../../THIRD_PARTY.md).
Source builds use the shared table, HAL, clock and fonts. App images are packaged
with `projects/launcher/tools/package_sd_app.py`; attach full notices. Console
is updated over USB at factory only after layout/backup verification.
