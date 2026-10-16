# Usage

**Ollama, Codex and your other AI services on one small screen.** Version
**0.2.0** replaces the single-provider Ollama Usage app. Separate source pages,
PixelFonts, dark/mint-blue themes and touch-only controls; no keyboard required.

This is an **early-access candidate**. The pinned ESP32 build, automated tests
and live host Ollama/Codex checks pass. On 16 October 2026 the user confirmed
SD installation, Codex meters and switching back to Ollama work on the tested CYD.
Screenshots use actual renderers with public test data, never private accounts.

## Sources

| Source | What it shows | Authentication |
|---|---|---|
| Ollama | Session/weekly used percentages and resets, or included credits; purchased credits and hourly 24H requests | Your Ollama API key; direct verified HTTPS |
| Codex | Actual five-hour/weekly windows, their reported durations and resets; available credit information | Your existing signed-in Codex CLI on a Mac/host, through a read-only HTTPS bridge |
| OpenRouter | API-key budget/remaining amount, daily/weekly key spending; no invented account-credit total | Optional OpenRouter key; direct verified HTTPS |
| Custom feed | Up to two percentage meters and three short statistics | Your authenticated HTTPS JSON feed, optionally using the bundled bridge |

Tap the **source name in the top bar** to select a service. Choose **+ ADD A
PROVIDER** for configuration guidance. Up to four sources are supported,
including several custom feeds with distinct IDs. Adding one provider does not
replace the others. Each has its own snapshot, freshness and retry policy.

### Existing Ollama users

Old `/private/ollama.json` continues to work if `/private/usage.json` is absent.
The old theme namespace is retained. To add sources, move to the new configuration
rather than overwriting your old key. If the new file exists but is invalid, it
is not silently replaced by the legacy configuration. Existing SD files, keys,
keyboard bonds and other apps are never erased by the firmware.

## Codex: reuse the current login

The bridge calls Codex's documented **`account/rateLimits/read`** over local
app-server stdio. It does not read/copy `auth.json`, start a model turn, perform
inference, redeem a reset, or make an OpenAI Platform API key. OAuth stays in
Codex on the host. Only normalized usage windows/statistics are served to CYD;
account IDs, emails, reset-credit IDs and tokens are excluded.

On a host with Python 3.10+, OpenSSL and an already signed-in Codex CLI:

```sh
python3 projects/usage/bridge/install_bridge.py --install-agent
```

The installer prepares ignored private settings, a local CA/server certificate,
a random read-only feed token and the device configuration. On macOS it installs
only this app's user LaunchAgent, `au.bespoke.cyd.usage`. It can reuse an existing
private Ollama JSON with `--legacy-ollama /PRIVATE/PATH/ollama.json`. If no Ollama
key is available it prepares Codex alone. No sign-in or key data is printed.
Without `--install-agent` it only prepares the files; another OS can run
`usage_bridge.py --config /PRIVATE/PATH/bridge.json` through its service manager.

Using an SD reader, copy these generated files from `artifacts/provisioning/`:

- `usage.json` → **`/private/usage.json`**
- `usage-ca.pem` → **`/private/usage-ca.pem`** (public trust certificate)

**Never copy** `bridge.json`, `server.key`, `ca.key` or your Codex OAuth tokens
to the SD card. The bridge's keys stay on the host. Private files are excluded
from the firmware/archive/repository. File Server does not expose `/private`,
so its browser cannot provision these files. FAT32 is not encrypted.

The bridge binds an explicit private IPv4 interface, uses TLS 1.2+, validates
feed tokens, serves only fixed read-only paths, bounds connections and caches
Codex reads for 60 seconds. The client trusts the supplied CA and verifies the
hostname/IP. No insecure TLS fallback or redirect forwarding is implemented.
The Mac must stay awake, the source project/SSD must remain available to its
LaunchAgent, and a changed LAN address may require certificate/configuration
regeneration. A DHCP reservation is useful. Do not expose it to the Internet.
The service is quiet; use `tools/check_sources.py` for safe status checks.

To stop the macOS helper, use `launchctl bootout gui/$(id -u)/au.bespoke.cyd.usage`.
Its plist is in the standard user LaunchAgents folder; do not stop unrelated
Codex services. The installer refuses to overwrite an unrelated agent.

## Add OpenRouter or another service

OpenRouter is the worked third-provider example. Add this entry to the private
`providers` array, replacing the placeholder in your private editor:

```json
{"type":"openrouter","id":"openrouter","label":"OpenRouter","api_key":"REPLACE_WITH_YOUR_KEY"}
```

No management/provisioning key is required for the read-only `/api/v1/key`
endpoint. The dashboard describes that key's budget and spending, not your
account's purchased credits. Missing metrics remain unavailable. It is tested
with fixtures; a real OpenRouter key was not requested or silently harvested.

For other services, add a `feed` entry with a unique short ID, label, HTTPS URL
and bearer token. `ca_file` is optional: omit it for a normal publicly trusted
certificate, or use a PEM under `/private/` for a private feed. IPv4/DNS hostnames
are supported; URL userinfo, queries, fragments, redirects and plain HTTP are
rejected. Example:

```json
{"type":"feed","id":"my-ai","label":"My AI","url":"https://example.com/v1/providers/my-ai","api_key":"REPLACE_WITH_YOUR_FEED_TOKEN"}
```

The endpoint returns the [small normalized schema](bridge/feed.example.json):
`schema:1`, matching `provider`, Unix `updated_at`, `meters` (at most two),
`stats` (at most three). Meter labels are at most 16 printable ASCII characters,
used percentages 0–100, optional reset seconds. Statistic labels are at most
16 characters, values at most 24. Use null for an unknown reset, not zero.
Generate current timestamps: the example's fixed date is deliberately stale.
The firmware rejects a feed over three minutes old or over two minutes ahead.

A local exporter can write that JSON atomically to a private file. Add
`{"id":"my-ai","file":"/PRIVATE/PATH/my-ai.json"}` to `feeds` in the bridge's
private `bridge.json`, then restart only this helper. The authenticated path
becomes `/v1/providers/my-ai`; reuse its feed token and CA in the device profile.
The bridge validates the file and strips unknown fields. Clients cannot select
arbitrary files, execute commands, or access a generic HTTP proxy.

Use [usage.example.json](usage.example.json) as a blank template. Delete unused
providers and add a `wifi` array only if you need to override saved profiles.

## Wi-Fi, installation and controls

Wi-Fi is read once from the new file or existing File Server/Terminal-compatible
SD profiles. Up to four 2.4 GHz WPA2/WPA3-Personal networks are supported. No
open/enterprise Wi-Fi or captive-portal sign-in. Use File Server's network setup
for another LAN and relaunch Usage; Console radio tiles are placeholders.

Copy the matching **Usage-0.2.0.bin**, **Usage-0.2.0.json** and
**Usage-0.2.0-LICENSE.txt** into SD `/firmware`. Then use **Console → Load from SD
Card → Usage-0.2.0 → Install + run**. Keep the clock-capable File Server slot.
Do not flash an app image over factory Console or follow IDF's generic flash hint.

| Control | Effect |
|---|---|
| Source name | Select a provider or view setup guidance. |
| REFRESH | Refresh when eligible; respects independent cooldowns. |
| 24H / DETAILS | Ollama hourly chart, or the current provider's reported details. |
| LIGHT / DARK | Change and retain the app theme. |
| NET → RETRY / NEXT | Rejoin / try the next saved Wi-Fi profile. |
| NET → RELOAD SD | Reread source keys, endpoints and CA files when idle; clear old account data. Wi-Fi changes require relaunching. |
| CONSOLE | Return to verified factory Console, including during a fetch. |

After five idle minutes the display/backlight sleeps and new polling pauses.
A released touch wakes it without activating a button. Wi-Fi stays running.
Source work is round-robin and separate from rendering; the touch targets are
at least 34 px. A successful provider waits 60 seconds after completion.
Failures back off to 60/120/240/480/600 seconds; numeric Retry-After is respected.
401/403 stop that source's automatic retries until a manual action. One bad
source does not disable the others. Old/failed/lost-link data is marked STALE.
Each HTTPS response is bounded to 24 KiB, depth and duplicate keys are checked,
and a valid shared clock is required before TLS. No account values are logged.

## Build and checks

From the repository root with the pinned ESP-IDF installed:

```sh
bash projects/usage/tests/run-native.sh
bash projects/usage/firmware/tools/build.sh build
python3 projects/usage/tools/check_sources.py --config /PRIVATE/PATH/usage.json
```

Native ASan/UBSan tests cover retained Ollama behavior, multi-provider/feed and
OpenRouter parsing, private config/legacy fallback, source selection and 34 px
targets, stale frames, bounded HTTP, certificate choice and fixed provider hosts.
Python tests exercise real HTTPS, certificate trust, authorization, read-only
routing, fresh/stale/custom feeds and the minimal Codex RPC protocol. Optional
live checks feed bodies directly into the actual C++ parser and print status
flags only; no API/inference charges are generated by a usage read.

Verified live: existing Codex login → HTTPS bridge → actual firmware parser, and
both Ollama endpoints → actual parser. OpenRouter/custom layouts are fixture
coverage. **Basic device check accepted:** SD install, Codex meters and switching back to
Ollama. **Still pending on physical CYD:** Run last, instrumented MCU heap,
source taps and readability, real reset accuracy, reconnect, sleep/wake and
Console return. A source build or host test does not prove hardware acceptance.

Official sources: [Codex app-server account limits](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt),
[OpenRouter current-key usage](https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key),
[Ollama OpenAPI](https://github.com/ollama/ollama/blob/main/docs/openapi.yaml).
Dependency notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
