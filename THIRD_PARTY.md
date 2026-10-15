# Attribution and source provenance

The original project code, documentation and site use the root MIT licence.
Vendored components and linked SDK code retain their own terms:

| Component | Pinned source | Licence and location |
|---|---|---|
| PixelFonts, mcer12 | [344a6cf](https://github.com/mcer12/PixelFonts/tree/344a6cfcd660b8546258485befb7576a47790d7d) | MIT; `projects/launcher/third_party/PixelFonts/LICENSE` |
| libssh2 ESP port, skuodi | [198771d](https://github.com/skuodi/libssh2_esp/tree/198771df0a67156a921a0acf6552d431462a0fdc) | BSD-3-Clause; `common/libssh2_esp/LICENSE` |
| libssh2 1.11.1 | [455f062](https://github.com/libssh2/libssh2/tree/455f062236fdd5deba9abb909dbe3b86463a23d8) | BSD-style; `common/libssh2_esp/libssh2/COPYING` |
| ESP-IDF 5.5.5 | [b774170](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f) | Apache-2.0 and component-specific terms; SDK installed separately; `distribution/ESP-IDF-LICENSE.txt` |

The generated WOFF2 fonts and firmware bitmap header derive from PixelFonts;
the full font licence accompanies both the site fonts and SD packages.
Application package `-LICENSE.txt` files contain redistribution notices.
The Console notice is `distribution/console/LICENSE.txt`.

The libssh2 port includes bounded receive/transmit staging adaptations; see
[its provenance](common/libssh2_esp/PROVENANCE.md). These support the configured
AES-CTR/no-compression SSH client; they do not establish arbitrary cipher or
large-message compatibility. No M5Launcher source, M5 board support or assets
are used. Console is a custom ESP-IDF launcher for this CYD profile.

## Ollama Usage

The app uses shared PixelFonts/HAL/clock/Wi-Fi code and the pinned ESP-IDF JSON
and TLS components. Its native tests vendor that SDK's cJSON source (MIT) under
`projects/ollama-cloud-usage/tests/third_party/cjson`, with its full notice.
The inspected browser extension is reference material; its MIT notice is retained
in the app's `THIRD_PARTY_NOTICES.md`. The firmware uses an independent official
API client, not the browser cookie/HTML scraper. The SD licence file preserves
PixelFonts, ESP-IDF, Mbed TLS, cJSON and reference notices.
