# Firmware and collaboration templates

## Start a new app

```sh
python3 tools/new_app.py --project HelloCYD --output projects/hello-cyd
bash projects/hello-cyd/firmware/tools/build.sh build
python3 projects/launcher/tools/package_sd_app.py --image projects/hello-cyd/firmware/build/HelloCYD.bin --output projects/hello-cyd/artifacts/sd-library
```

The scaffold owns a theme namespace, uses shared HAL/clock/PixelFonts and
Console return, and confirms OTA only after startup checks. Add full notices;
the packager creates image and JSON only. Read `examples/minimal-app.cpp`.

## Change brief

- Human goal and out-of-scope work:
- Current accepted version and private evidence location:
- Behaviour before → after:
- Hardware or privacy constraints:
- Native/build checks:
- Short physical check and expected observable result:

## App README

```markdown
# App name
Purpose / latest version / accepted or candidate.
## Use
Install, controls, themes, return to Console.
## Configuration
Private SD schema, provisioning and failure behaviour; no real secrets.
## Build and test
Exact commands from checkout root.
## Validation
Build + native evidence, separate physical acceptance, unperformed cases.
## Limits and licences
Memory, hardware, network constraints and required upstream notices.
```

## Release record

```markdown
Date / app / version / source commit:
Image name / byte length / SHA-256:
SDK pin / shared-code changes:
Build and native tests:
Hardware identity verified privately:
Install + run / Console / Run last / recovery / retained bonds:
App acceptance result:
Unperformed checks and limits:
Package + JSON + complete notices reviewed:
```

## Handoff checkpoint

```markdown
Date and task:
User-authorized scope:
Files changed and reason:
Verified facts, commands and output artefacts:
Physical acceptance (who observed what):
Pending work / known failures:
Exact next action:
Private evidence path (local only; never publish its contents):
```

Placeholders are placeholders, never PASS evidence. Humans confirm actual
readability/device outcomes; agents build, test, explain and retain continuity.
