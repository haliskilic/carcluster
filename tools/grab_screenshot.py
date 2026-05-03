#!/usr/bin/env python3
"""
grab_screenshot.py — carcluster ekran görüntüsü yakalama aracı.

Cihazda Settings → Diag → Screenshot butonu UART'a base64 RGB565 dump eder.
Bu script o stream'i dinler, decode eder, PNG olarak kaydeder.

Kullanım:
    # Önce idf.py monitor'u KAPAT (UART tek seferde tek client)
    python3 tools/grab_screenshot.py [PORT] [OUT.png]

    # Default: /dev/ttyACM0, screenshot.png
    python3 tools/grab_screenshot.py
    python3 tools/grab_screenshot.py /dev/ttyACM0 main.png

Sonra cihazda Settings (long-press) → Diag → Screenshot butonuna bas.
~1.5 dakika içinde PNG hazır olur (768KB @ 115200 baud).

Bağımlılıklar: pyserial, Pillow
    pip install pyserial Pillow
"""
import sys, re, base64, struct
import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "screenshot.png"
BAUD = 115200

BEGIN_RE = re.compile(rb"\[SHOT-BEGIN W=(\d+) H=(\d+) FMT=(\w+) BYTES=(\d+)\]")
END_TOKEN  = b"[SHOT-END]"
ERR_TOKEN  = b"[SHOT-ERROR"

def rgb565_to_rgb888(buf, w, h):
    """RGB565 little-endian → PIL RGB image."""
    img = Image.new("RGB", (w, h))
    pixels = img.load()
    idx = 0
    for y in range(h):
        for x in range(w):
            pix = struct.unpack_from("<H", buf, idx)[0]
            r5 = (pix >> 11) & 0x1F
            g6 = (pix >> 5)  & 0x3F
            b5 =  pix        & 0x1F
            # 5/6 bit → 8 bit (replicate high bits in low for accuracy)
            r = (r5 << 3) | (r5 >> 2)
            g = (g6 << 2) | (g6 >> 4)
            b = (b5 << 3) | (b5 >> 2)
            pixels[x, y] = (r, g, b)
            idx += 2
    return img

def main():
    print(f"[host] listening on {PORT} @ {BAUD}", file=sys.stderr)
    print(f"[host] press Settings → Diag → Screenshot on device", file=sys.stderr)
    ser = serial.Serial(PORT, BAUD, timeout=10)

    capturing = False
    w = h = bytes_expected = 0
    fmt = ""
    buf = bytearray()

    while True:
        line = ser.readline()
        if not line:
            continue
        if not capturing:
            m = BEGIN_RE.search(line)
            if m:
                w = int(m.group(1))
                h = int(m.group(2))
                fmt = m.group(3).decode()
                bytes_expected = int(m.group(4))
                buf = bytearray()
                capturing = True
                print(f"[host] capturing {w}x{h} {fmt} ({bytes_expected} bytes)...",
                      file=sys.stderr)
                continue
            if ERR_TOKEN in line:
                print(f"[host] device error: {line.decode(errors='ignore').strip()}",
                      file=sys.stderr)
                sys.exit(1)
            # Ignore all other log noise pre-capture
            continue

        # capturing
        if END_TOKEN in line:
            print(f"[host] received {len(buf)} bytes, decoding...", file=sys.stderr)
            if len(buf) < bytes_expected:
                print(f"[host] WARNING: short read ({len(buf)} < {bytes_expected})",
                      file=sys.stderr)
            if fmt != "RGB565":
                print(f"[host] unsupported format: {fmt}", file=sys.stderr)
                sys.exit(1)
            img = rgb565_to_rgb888(buf, w, h)
            img.save(OUT)
            print(f"[host] saved {OUT}", file=sys.stderr)
            return

        # Try base64 decode — if line is data
        try:
            line_clean = line.strip()
            if line_clean:
                buf.extend(base64.b64decode(line_clean, validate=True))
            # Progress every ~64KB
            if len(buf) % 65536 < 80 and len(buf) > 0:
                pct = 100 * len(buf) // bytes_expected if bytes_expected else 0
                print(f"\r[host] {len(buf)/1024:.0f} KB / {bytes_expected/1024:.0f} KB  ({pct}%)",
                      end="", file=sys.stderr)
        except Exception:
            # Probably a log line mixed in, skip
            pass

if __name__ == "__main__":
    main()
