# Release status

| Firmware | Released binary | Evidence from the original tested CYD |
|---|---|---|
| Console | 0.3.3-sleep, 336000 bytes | SD installs / Run last / touch recovery; theme and complete layout; shutdown stays dark and touch wakes |
| File Server | 0.1.2-clock, 921888 bytes | Upload, download, cancel; themes and readable code; clock helper |
| Terminal Micro | 0.2.3-headroom, 1376832 bytes | B1/touch/readability; SSH shells on two LAN hosts; high-output remote TUI remains connected; SD install and Run last |

Evidence is user-observed acceptance plus private redacted-result records;
raw UART, network identities and credentials are deliberately not published.
Console sleep has no measured current/power claim. File Server's full phone
walkthrough joining a fresh LAN remains pending; browser fixtures do not prove
it. No universal keyboard, CYD clone or full xterm compatibility claim.

## Public source versus prebuilt release

The release preserves the physically accepted binaries byte-for-byte. The
public source extracts shared terminal drivers into `common/` and makes build
wrappers portable. Current shared HAL source also contains later Console sleep
support. Consequently a source rebuild is a **candidate**, not bit-identical
to the older accepted app binary. Public-layout builds and native/browser
checks are recorded in `validation.json`; new firmware changes need their own
physical acceptance before replacing accepted release packages.

Console radio tiles and Tailscale are placeholders. No native USB disk mode.
The excluded legacy app and other private apps are not part of this edition.

## Usage 0.2.0 — current early access

Renamed from Ollama Usage, adding Codex, optional OpenRouter and custom feeds.
Prepared image: 1,050,400 bytes. Current files are under `distribution/sd/firmware`.
The previous Ollama 0.1.0 package remains in its historical GitHub release/tag.

Pinned public SDK build and native ASan/UBSan model/config, feed/OpenRouter,
source UI and actual HTTP-code checks pass. Python tests exercise real HTTPS,
CA trust, bearer auth, read-only routing, stale/custom feeds and Codex's minimal
RPC. Both live Ollama endpoints and the existing-login Codex HTTPS feed passed
the actual C++ parser on the development host. Public screenshots are fixtures.
No OAuth tokens or real private files are included. OpenRouter is fixture-tested;
no account key was requested. See the app guide for the read-only host bridge.

Pending physical CYD acceptance: install/Run last, actual MCU TLS/memory, source
selection/readability, reset accuracy, reconnect, display sleep and Console
return. This does not supersede the three accepted binaries above. Public source
rebuilds remain candidates, not guaranteed bit-identical to the prepared image.
