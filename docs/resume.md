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
