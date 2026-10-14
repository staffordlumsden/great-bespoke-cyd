#!/usr/bin/env python3
"""Adapt pinned MIT PixelFonts bitmap data for the small ESP-IDF renderer."""
from pathlib import Path
import re


def generate():
    firmware = Path(__file__).resolve().parents[1]
    source = firmware.parent / "third_party/PixelFonts/src"
    faces = [("7x5_thin", 7), ("11x5_thin", 11), ("13x7_semibold", 13)]
    output = [
        "// Generated from mcer12/PixelFonts at 344a6cfcd660b8546258485befb7576a47790d7d.",
        "// Copyright (c) 2026 mcer12. MIT; see third_party/PixelFonts/LICENSE.",
        "// Run firmware/tools/generate_pixel_fonts.py to regenerate.",
        "#pragma once", "#include <cstdint>", "namespace cyd_fonts {",
        "struct Glyph { uint16_t offset; uint8_t width,height,advance; int8_t x,y; };",
        "struct Font { const uint8_t* bitmap; const Glyph* glyphs; uint16_t first,last; uint8_t ascent; };",
    ]
    for suffix, ascent in faces:
        name = "pixelfont_" + suffix
        data = (source / (name + ".h")).read_text()
        pixels = re.search(r"const uint8_t " + name + r"_Bitmaps\[\] PROGMEM = \{(.*?)\};", data, re.S)
        glyphs = re.search(r"const GFXglyph " + name + r"_Glyphs\[\] PROGMEM = \{(.*?)\};", data, re.S)
        if not pixels or not glyphs:
            raise ValueError("unexpected upstream format: " + name)
        bitmap = [int(n.strip(), 0) for n in pixels.group(1).split(",") if n.strip()]
        metric_data = re.sub(r"//[^\n]*", "", glyphs.group(1))
        metrics = [[int(n.strip(), 0) for n in m.split(",")] for m in re.findall(r"\{([^}]+)\}", metric_data)]
        if len(metrics) != 95:
            raise ValueError("expected printable ASCII: " + name)
        for offset, w, h, advance, x, y in metrics:
            if offset + (w*h + 7)//8 > len(bitmap):
                raise ValueError("bitmap bounds: " + name)
        output.append("inline constexpr uint8_t " + name + "_bits[] = {")
        for start in range(0, len(bitmap), 16):
            output.append("    " + ",".join(f"0x{n:02x}" for n in bitmap[start:start+16]) + ",")
        output.append("};")
        output.append("inline constexpr Glyph " + name + "_glyphs[] = {")
        output.extend("    {" + ",".join(str(n) for n in g) + "}," for g in metrics)
        output.append("};")
        output.append("inline constexpr Font " + name + " = {" + name + "_bits," + name + "_glyphs,32,126," + str(ascent) + "};")
    output.append("} // namespace cyd_fonts")
    destination = firmware / "main/pixel_fonts.h"
    destination.write_text("\n".join(output) + "\n")
    print("Generated " + str(destination))


if __name__ == "__main__":
    generate()
