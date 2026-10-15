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
