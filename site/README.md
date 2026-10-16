# Static project site

Serve this folder over HTTP for local testing. No bundler, CDN, analytics,
remote font requests or credentials are required. PixelFonts are self-hosted
with their licence. `theme.js` persists the selected theme locally.

Screenshots come from actual firmware renderers/native fixtures and the File
Server browser UI served by its mock test API. They contain public demo data;
they are not photographs or evidence of new physical tests. `tools/screenshots.py`
regenerates the device/browser assets after tests have run. The device frame
on the hero is decorative CSS, not a hardware photo.

The page leads with a compact project introduction, then two small featured
release cards. Detailed app guides follow; Usage stays below the other apps.
Keep thumbnails proportional, use content-driven card heights and avoid large
spacers or duplicate overview rows. The browser check covers five widths, both
themes and explicit limits on the release panel height/gap. When replacing the
stylesheet, update its version query in index.html so returning visitors fetch
the matching CSS rather than a cached older layout.
