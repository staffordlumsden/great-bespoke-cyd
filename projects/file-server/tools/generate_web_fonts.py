#!/usr/bin/env python3
"""Turn the pinned Console PixelFonts into compact browser WOFF2 fonts."""
from pathlib import Path
import re
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

root = Path(__file__).resolve().parents[3]
source = root / "projects/launcher/third_party/PixelFonts/src"
destination = Path(__file__).resolve().parents[1] / "firmware/web"
destination.mkdir(parents=True, exist_ok=True)
for suffix, filename in (("11x5_thin", "pixel-thin.woff2"), ("13x7_semibold", "pixel-semibold.woff2")):
    data = (source / ("pixelfont_" + suffix + ".h")).read_text()
    pixels = re.search(r"Bitmaps\[\] PROGMEM = \{(.*?)\};", data, re.S)
    metrics = re.search(r"Glyphs\[\] PROGMEM = \{(.*?)\};", data, re.S)
    bitmap = [int(x.strip(), 0) for x in pixels.group(1).split(",") if x.strip()]
    rows = [[int(x.strip()) for x in row.split(",")] for row in re.findall(r"\{([^}]+)\}", re.sub(r"//[^\n]*", "", metrics.group(1)))]
    names = [".notdef"] + [f"uni{c:04X}" for c in range(32, 127)]
    glyphs, advances = {}, {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()
    advances[".notdef"] = (512, 0)
    for c, row in zip(range(32, 127), rows):
        offset, width, height, advance, gx, gy = row
        pen = TTGlyphPen(None)
        for y in range(height):
            for x in range(width):
                bit = y * width + x
                if bitmap[offset + bit // 8] & (0x80 >> (bit % 8)):
                    left, bottom = (gx + x) * 64, -(gy + y + 1) * 64
                    pen.moveTo((left, bottom)); pen.lineTo((left, bottom + 64))
                    pen.lineTo((left + 64, bottom + 64)); pen.lineTo((left + 64, bottom)); pen.closePath()
        name = f"uni{c:04X}"
        glyphs[name] = pen.glyph()
        advances[name] = ((advance + 1) * 64, gx * 64)
    font = FontBuilder(1024, isTTF=True)
    font.setupGlyphOrder(names); font.setupCharacterMap({c: f"uni{c:04X}" for c in range(32, 127)})
    font.setupGlyf(glyphs); font.setupHorizontalMetrics(advances)
    font.setupHorizontalHeader(ascent=832, descent=-192)
    font.setupNameTable({"familyName": "CYD PixelFonts " + suffix, "styleName": "Regular", "uniqueFontIdentifier": "mcer12-PixelFonts-344a6cfc-" + suffix, "fullName": "CYD PixelFonts " + suffix, "psName": "CYDPixel-" + suffix.replace("_", "-"), "version": "Version 1.0", "copyright": "Copyright 2026 mcer12. MIT License. Adapted from PixelFonts."})
    font.setupOS2(sTypoAscender=832, sTypoDescender=-192, usWinAscent=832, usWinDescent=192)
    font.setupPost(); font.setupMaxp(); font.font.flavor = "woff2"; font.save(destination / filename)
    print(filename, (destination / filename).stat().st_size)
