# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

#!/usr/bin/env python3
"""Receive screenshot / diagnostic from ESP32 over serial.

Usage:
    python3 recv_screenshot.py <serial_port> [output.bmp]

Double-click BOOT on ESP32 to trigger screenshot.
"""
import re, struct, sys, time
import serial

def save_bmp(data, pw, ph, path):
    """Save rotated BMP: physical (pw x ph) → logical (ph x pw)."""
    w, h = ph, pw  # logical dimensions after 90° rotation
    row_size = ((w * 3 + 3) // 4) * 4
    pixel_offset = 14 + 40
    file_size = pixel_offset + row_size * h
    buf = bytearray(file_size)
    struct.pack_into("<2sIHHI", buf, 0, b"BM", file_size, 0, 0, pixel_offset)
    struct.pack_into("<IiiHHIIiiII", buf, 14, 40, w, h, 1, 24, 0, 0, 0, 0, 0, 0)
    # Read physical framebuffer in logical order (90° CW rotation)
    # Logical pixel (lx, ly) = physical framebuffer[lx * pw + (pw - 1 - ly)]
    for ly in range(h):  # logical row (BMP row, top-to-bottom in data = bottom in file)
        dst_row = pixel_offset + (h - 1 - ly) * row_size
        for lx in range(w):
            off = (lx * pw + (pw - 1 - ly)) * 2
            v = struct.unpack_from("<H", data, off)[0]
            r, g, b = ((v>>11)&0x1F)<<3, ((v>>5)&0x3F)<<2, (v&0x1F)<<3
            buf[dst_row + lx*3:dst_row + lx*3+3] = bytes([b, g, r])
    with open(path, "wb") as f:
        f.write(buf)

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    ser = serial.Serial(port, 115200, timeout=30, dsrdtr=False, rtscts=False)
    ser.dtr = False  # Don't reset ESP on connect
    print(f"Listening on {port} — double-click BOOT for screenshot")
    state, buf = "idle", b""
    while True:
        data = ser.read(4096)
        if not data:
            continue
        if state == "idle":
            buf += data
            # Check for SCREENSHOT marker BEFORE splitting lines
            m = re.search(rb"SCREENSHOT:(\d+)x(\d+):", buf)
            if m:
                w, h = int(m.group(1)), int(m.group(2))
                nbytes = w * h * 2
                buf = buf[m.end():]
                print(f"\nScreenshot: {w}x{h} ({nbytes} bytes)...")
                # Physical dimensions: w x h from header, data is raw framebuffer
                pw, ph = w, h
                w, h = ph, pw  # logical = rotated 90°
                state, sofar = "screenshot", len(buf)
                continue
            # Print lines while keeping any partial line
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                print(line.decode("utf-8", errors="replace"))
        if state == "screenshot":
            buf += data
            if len(buf) >= nbytes:
                raw = buf[:nbytes]
                if not out:
                    out = f"screenshot_{time.strftime('%Y%m%d_%H%M%S')}.bmp"
                save_bmp(raw, pw, ph, out)
                print(f"Saved {w}x{h} to {out}")
                buf = buf[nbytes:]
                out, state = None, "idle"
            elif len(buf) % 8192 == 0:
                print(f"  {len(buf)*100//nbytes}%", flush=True)

if __name__ == "__main__":
    sys.exit(main())
