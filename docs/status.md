# Release status — 2026-10-14

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
