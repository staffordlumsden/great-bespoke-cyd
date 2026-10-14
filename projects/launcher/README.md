# CYD Console

Accepted prebuilt release: **0.3.3-sleep**. Rebuilt sources require new physical
acceptance. Screenshots are actual firmware-renderer/browser captures with
public fixture data, not photographs or live private sessions.

A resident touch launcher: theme, clock, deep-sleep touch wake, SD library,
verified app install, Run last and touch-at-boot recovery. Radio tiles are
placeholders. Load File Server first for the retained clock-sync helper.

![Console renderer](../../site/assets/screenshots/console-dark.png)

Select Load from SD Card → package → Install + run. Apps return via Console.
Do not write app-only images into factory. See the root installation guide.

## Build and test

From repository root:

```sh
bash tools/setup-idf.sh
bash projects/launcher/firmware/tools/build.sh build
bash tools/test.sh
```

Read [installation](../../docs/install.md), [private configuration](../../docs/configuration.md),
[release status](../../docs/status.md) and [third-party rights](../../THIRD_PARTY.md).
Source builds use the shared table, HAL, clock and fonts. App images are packaged
with `projects/launcher/tools/package_sd_app.py`; attach full notices. Console
is updated over USB at factory only after layout/backup verification.
