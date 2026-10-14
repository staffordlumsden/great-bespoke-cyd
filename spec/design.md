# Visual and interaction language

Use flat, rounded panels with crisp pixel lettering and line icons. Console,
File Server and Terminal Micro should feel like one small toolkit, with the
same title hierarchy, generous touch controls and a visible Console return.

## Fonts

Use the pinned mcer12 PixelFonts in `projects/launcher/third_party/PixelFonts`.
The generated bitmap header is `projects/launcher/firmware/main/pixel_fonts.h`;
File Server's WOFF2 fonts derive from the same source. Preserve the MIT notice.
Use 13x7 semibold for titles, 11x5 thin for main controls, 7x5 thin for secondary
metadata. Do not replace readable controls with the tiny terminal bitmap font.
Terminal uses 11x5 thin by default, with smaller/larger choices and PTY resize.

## Theme tokens

| Role | Dark | Mint/blue light (apps and site) |
|---|---|---|
| Background | #081115 | #A9DCC2 |
| Surface | #172B32 | #D6EFE7 |
| Foreground | #E4EEF0 | #0C3D48 |
| Muted | #91A9B0 | #386576 |
| Primary | #42A5F5 | #147BA4 |
| Success | #66CF8E | #176648 |
| Warning | #FFAE42 | #825000 |
| Error | #EF6B72 | #A52437 |

These guide new surfaces; accepted Console firmware retains its existing
light palette. Do not recolour accepted binaries solely for documentation.
ANSI terminal palette remains distinct, with bright enough blue on dark.

## Geometry and behaviour

- Landscape 320×240. Shared draw/hit definitions; no clipping at right/bottom.
- Prefer 40–46 px-high actions; Terminal touch targets are at least 34 px.
- Labels plus icons, and labels plus state colours. No colour-only meaning.
- Full-screen theme changes; persist each app's own preference namespace.
- Stable menus repaint only on change. Debounce resistive release and stop
  gestures from triggering buttons accidentally. Disabled controls are inert.
- Progress states show direction/file/bytes, completion, cancel or error.
- Keep terminal zoom, Live output and Console return easy to discover.
- Screenshots must have public fixture data and an honest capture caption.
