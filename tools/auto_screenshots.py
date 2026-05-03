#!/usr/bin/env python3
"""
auto_screenshots.py — carcluster ekranlarını otomatik yakala.

Cihazın cmd_listener'ı USB-Serial-JTAG üzerinden komut dinler. Bu script
sırayla SHOT MAIN / SHOT MODAL <tab> komutları gönderir, dönüşte gelen
[SHOT-BEGIN ... SHOT-END] base64 RGB565 stream'i decode edip PNG'ye çevirir.

Kullanım:
    # Önce idf.py monitor'u KAPAT (UART tek seferde tek client)
    python3 tools/auto_screenshots.py [PORT] [OUT_DIR]

    # Default: /dev/ttyACM0, docs/img
    python3 tools/auto_screenshots.py
    python3 tools/auto_screenshots.py /dev/ttyACM0 docs/img

Tüm ekranlar 5 dosyaya kaydedilir:
    main.png             — ana cluster ekranı (modal kapalı)
    modal-trip.png       — Settings → Trip sekmesi
    modal-display.png    — Settings → Display sekmesi
    modal-limits.png     — Settings → Limits sekmesi
    modal-diag.png       — Settings → Diag sekmesi

Bağımlılıklar: pyserial, Pillow
"""
import sys, os, re, base64, struct, time
import serial
from PIL import Image

PORT    = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
OUT_DIR = sys.argv[2] if len(sys.argv) > 2 else "docs/img"
BAUD    = 115200

SHOTS = [
    ("SHOT MAIN",          "main.png"),
    ("SHOT MODAL TRIP",    "modal-trip.png"),
    ("SHOT MODAL DISPLAY", "modal-display.png"),
    ("SHOT MODAL LIMITS",  "modal-limits.png"),
    ("SHOT MODAL DIAG",    "modal-diag.png"),
]

BEGIN_RE = re.compile(rb"\[SHOT-BEGIN W=(\d+) H=(\d+) FMT=(\w+) BYTES=(\d+)\]")
END_TOKEN  = b"[SHOT-END]"
ERR_TOKEN  = b"[SHOT-ERROR"

def rgb565_to_rgb888(buf, w, h):
    img = Image.new("RGB", (w, h))
    pixels = img.load()
    idx = 0
    for y in range(h):
        for x in range(w):
            pix = struct.unpack_from("<H", buf, idx)[0]
            r5 = (pix >> 11) & 0x1F
            g6 = (pix >>  5) & 0x3F
            b5 =  pix        & 0x1F
            r = (r5 << 3) | (r5 >> 2)
            g = (g6 << 2) | (g6 >> 4)
            b = (b5 << 3) | (b5 >> 2)
            pixels[x, y] = (r, g, b)
            idx += 2
    return img

def grab_one(ser, cmd, out_path):
    print(f"\n[host] -> {cmd}", file=sys.stderr)
    # Drain anything pending
    ser.reset_input_buffer()
    ser.write(f"{cmd}\n".encode())
    ser.flush()

    capturing = False
    w = h = bytes_expected = 0
    fmt = ""
    buf = bytearray()
    deadline = time.time() + 240    # 4 min per shot
    while time.time() < deadline:
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
                print(f"[host] capturing {w}x{h} {fmt} ({bytes_expected} bytes)", file=sys.stderr)
                continue
            if ERR_TOKEN in line:
                print(f"[host] device error: {line.decode(errors='ignore').strip()}", file=sys.stderr)
                return False
            continue
        # capturing
        if END_TOKEN in line:
            print(f"[host] received {len(buf)} bytes, decoding...", file=sys.stderr)
            if fmt != "RGB565":
                print(f"[host] unsupported format: {fmt}", file=sys.stderr)
                return False
            img = rgb565_to_rgb888(buf, w, h)
            img.save(out_path)
            print(f"[host] saved {out_path}", file=sys.stderr)
            return True
        try:
            line_clean = line.strip()
            if line_clean:
                buf.extend(base64.b64decode(line_clean, validate=True))
            if len(buf) % 65536 < 80 and len(buf) > 0:
                pct = 100 * len(buf) // bytes_expected if bytes_expected else 0
                print(f"\r[host] {len(buf)/1024:.0f} KB / {bytes_expected/1024:.0f} KB ({pct}%)",
                      end="", file=sys.stderr)
        except Exception:
            pass
    print(f"[host] TIMEOUT for {cmd}", file=sys.stderr)
    return False

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"[host] connecting to {PORT} @ {BAUD}", file=sys.stderr)
    ser = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(0.5)   # device USB-CDC stabilize

    ok = 0
    for cmd, fname in SHOTS:
        out = os.path.join(OUT_DIR, fname)
        if grab_one(ser, cmd, out):
            ok += 1
        time.sleep(0.3)   # gap between commands

    print(f"\n[host] done — {ok}/{len(SHOTS)} screenshots captured to {OUT_DIR}/",
          file=sys.stderr)
    return 0 if ok == len(SHOTS) else 1

if __name__ == "__main__":
    sys.exit(main())
