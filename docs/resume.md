# Public-project handoff

2026-10-14: first public edition prepared for `staffordlumsden/great-bespoke-cyd`.
Scope: Console, File Server, Terminal Micro, Pages site, screenshots and portable
firmware-development resources. Private workspace/card data are not an input
to this repository. The accepted hardware is left untouched by publication.

Start with root AGENTS.md, spec/SPEC.md and docs/status.md. Inspect validation.json
for actual public-layout checks; distribution hashes identify accepted binaries.
Shared components now live under common/. Existing compatible SD key paths and
schema tokens intentionally remain unchanged; see docs/configuration.md.

Next development must be driven by a human request, use the same pinned SDK,
retain themes/fonts/large touch targets and verify any source rebuild on actual
hardware. Do not repeat accepted user tests merely to fill a checklist. For a
new board, verify pins, display geometry, identity and flash layout first.

Public site is deployed at https://staffordlumsden.github.io/great-bespoke-cyd/.
Live Playwright checks passed desktop/tablet/mobile/320 px, themes/persistence,
actual fonts, every screenshot and navigation. Linux temporary-directory naming
was corrected without changing firmware. Repository publication contains no
SDK symlink, private configuration, device logs or excluded application folders.

## Ollama Cloud Usage 0.1.0 early-access publication

Added the app's portable source, optional local API checker, blank configuration,
native tests and pinned cJSON test dependency. Public builds reuse shared Wi-Fi
code; no excluded application source or private provisioning is included.
The prepared SD candidate is preserved at 1,032,400 bytes and SHA-256
`881ab28385f34b1b0b586af4f9463db708759043c953e4678fd8d324514fafe8`.

README and Pages now feature a prominent NEW RELEASE / EARLY ACCESS banner,
direct app download, setup steps and six public renderer captures. Site theme
also changes paired device captures. Public ESP32 build, full native suite,
release integrity and both browser suites pass locally. Original accepted
binaries remain unchanged. Physical CYD acceptance for Ollama Usage is pending;
this is an explicitly separate prerelease, not a replacement accepted release.
See GitHub Releases and Actions for current publication/deployment results.

## 2026-10-16 — Usage 0.2.0 multi-provider candidate

Renamed the app/project to Usage. Ollama remains a direct HTTPS provider;
Codex uses an authenticated, verified HTTPS host bridge reading only the
signed-in CLI's account/rateLimits/read RPC. OAuth remains on the host. Optional
OpenRouter key-budget/spending and bounded custom JSON feeds are supported;
these two adapters are fixture-tested, not live-account verified. Up to four
sources have independent snapshots, polling/retry and freshness. Source picker,
34 px touch controls, both palettes, PixelFonts and legacy Ollama config/theme
compatibility are retained. No OpenAI Platform API key is required.

Pinned private and portable public builds pass. Image is 1,050,400 bytes;
SHA-256 217fa28c529e37d1e4521651acac3e5d4fad21ced0ad170f0ea1345d7d11277e.
Native sanitizer/HTTP/provider tests and seven Python bridge TLS/RPC tests pass.
Public source checks and browser tests pass in four viewports and both themes;
actual renderer fixture captures inspected. Live Ollama endpoints and verified
Codex bridge HTTPS return 200 and pass the actual firmware parsers. No account
values are persisted in public evidence. Hardware acceptance remains pending.

Private development-card provisioning was byte-verified and safely ejected;
existing files were preserved. No real configuration is distributed.
Only the read-only feed token and public CA reach the card; host private keys,
Codex OAuth and bridge configuration stay on the Mac. No USB flash or partition
change. Next: Console → Load from SD Card → Usage-0.2.0 → Install + run; select
Codex and switch back to Ollama. Record observed results before accepting the
candidate. ESP32 TLS/memory, touch, reconnect, sleep and Console/Run last still
need device evidence. Existing accepted release/apps are unchanged.

## 2026-10-16 — Usage 0.2.0 basic device check accepted

User answered “all good” to the Console SD Install + run, Codex usage-meter
and switch-back-to-Ollama check. Record those specific behaviours as accepted
on the tested CYD. This confirms the normal device HTTPS data path works;
no instrumented heap/stack or independent reset-accuracy measurement was taken.
OpenRouter/custom live accounts, Run last, reconnect, sleep/wake and Console
return were not separately tested in this check. Keep the release early access.
No firmware bytes, private settings, keys or device state changed for this record.
