---
name: great-bespoke-cyd
description: Build, review, test and package ESP-IDF firmware apps for the original 4 MB ESP32 CYD and Great Bespoke CYD Console. Use for this platform's launcher, SD installers, touchscreen UI, BLE keyboard, SSH terminal, file-transfer, weather or daily-note apps; not for generic ESP32-S3/M5 firmware.
---

# Great Bespoke CYD firmware workflow

Use this skill within a Great Bespoke CYD checkout. Locate its root by
`AGENTS.md`, `partitions.csv` and `projects/launcher`. Do not rely on the skill
being installed inside the repository or on a personal absolute path.

1. Read root `AGENTS.md`, `spec/SPEC.md`, `docs/status.md`, `docs/resume.md`
   and the relevant app README. Preserve the human's current scope.
2. Use `spec/reference.md` for pins, flash layout, version/toolchain contracts;
   `spec/design.md` for fonts, palette and interaction; `spec/templates.md`
   for acceptance/release/handoff records. Read only the needed resources.
3. For a new app, run `python3 tools/new_app.py --project HelloCYD --output
   projects/hello-cyd`. It refuses overwrites. Its minimal source is
   `spec/examples/minimal-app.cpp`; generated code is not hardware accepted.
4. Run the app's pinned build wrapper, then relevant native tests. Use
   `bash tools/test.sh` for shared changes. Inspect memory, slot size and
   actual hit targets; retain full licence notices in packages. For new firmware,
   keep large buffers off task stacks, measure runtime margins and confirm
   pending OTA only after the first successful rendered frame.
5. Package with `python3 projects/launcher/tools/package_sd_app.py --image
   projects/<app>/firmware/build/<Project>.bin --output
   projects/<app>/artifacts/sd-library`. Never treat this app image as Console.
6. Before authorized device work follow `docs/install.md`: identify, back up,
   stop serial captures, write only the intended region, verify readback,
   test return/recovery and retain NVS/other slot. Do not infer permission
   from this skill. Do not erase NVS or disable SSH host-key verification.
7. Record host tests separately from physical acceptance. Provide short,
   readable device checks; protect private credentials and screenshots.
8. Update status/resume and app documentation before handoff. Leave the exact
   version/hash, tests, pending checks and next action. No guessed evidence.
