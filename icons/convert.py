# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

#!/usr/bin/env python3
"""Convert 20x20 RGBA PNGs to 16-bit 565 C arrays for ESP32 firmware."""
from PIL import Image
import os, glob

icons = {
    "bus.png": "bus",
    "sbahn.png": "suburban",
    "tram.png": "tram",
    "ubahn.png": "subway",
}

rows = []
for fname, product in sorted(icons.items()):
    img = Image.open(fname).convert("RGBA")
    w, h = img.size
    assert w == 20 and h == 20
    pixels_rgb565 = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            # If transparent (alpha < 128), make it black (transparent on black bg)
            if a < 128:
                r = g = b = 0
            # RGB565 conversion
            r5 = (r * 249 + 1014) >> 11  # 8-bit to 5-bit
            g6 = (g * 253 + 505) >> 10   # 8-bit to 6-bit
            b5 = (b * 249 + 1014) >> 11  # 8-bit to 5-bit
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            pixels_rgb565.append(rgb565)
    # Format as C array
    lines = []
    lines.append(f"// {fname} -> {product}")
    lines.append(f"static const uint16_t ICON_{product}[{w}*{h}] = {{")
    for i, px in enumerate(pixels_rgb565):
        sep = "," if (i + 1) % 8 else ",\n"
        lines[-1] += f" 0x{px:04X}{sep}" if i % 8 == 0 else f" 0x{px:04X}{sep}"
    lines.append("};\n")
    rows.extend(lines)

with open("TransportIcons.h", "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define ICON_W 20\n#define ICON_H 20\n\n")
    for line in rows:
        f.write(line + "\n")
        # Add a newline after each array
    f.write("""
static inline const uint16_t* icon_for_product(const char* product) {
  if (strcmp(product, "bus") == 0) return ICON_bus;
  if (strcmp(product, "tram") == 0) return ICON_tram;
  if (strcmp(product, "suburban") == 0) return ICON_suburban;
  if (strcmp(product, "subway") == 0) return ICON_subway;
  return ICON_subway; // default
}
""")
print("Generated TransportIcons.h")
