#!/usr/bin/env python3
"""
ota_serve.py — yerel ağda build/carcluster.bin için HTTP server.

Cihaz UART üzerinden 'OTA URL http://HOST:PORT/carcluster.bin' + 'OTA START'
komutu alır, bu HTTP server'dan binary'i indirir, ota_0/ota_1 partition'a
yazar, set_boot + restart yapar.

Kullanım:
    # Build dizininde carcluster.bin olmalı
    python3 tools/ota_serve.py [PORT]
    # Default port 8000

    # Sonra başka terminalde:
    python3 -c "import serial; s=serial.Serial('/dev/ttyACM0', 115200); \\
                s.write(b'OTA URL http://192.168.1.X:8000/carcluster.bin\\nOTA START\\n')"
"""
import sys, os, http.server, socketserver, socket

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")

if not os.path.isfile(os.path.join(ROOT, "carcluster.bin")):
    print(f"[server] FATAL: {ROOT}/carcluster.bin not found — run idf.py build first")
    sys.exit(1)

# Find local LAN IP (first non-loopback)
def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()

ip = lan_ip()
size_kb = os.path.getsize(os.path.join(ROOT, "carcluster.bin")) // 1024

os.chdir(ROOT)
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write(f"[server] {self.address_string()} - {fmt % args}\n")

print(f"[server] serving {ROOT}/carcluster.bin ({size_kb} KB) on port {PORT}")
print(f"[server] device URL:  http://{ip}:{PORT}/carcluster.bin")
print(f"[server] trigger:")
print(f"  python3 -c \"import serial; s=serial.Serial('/dev/ttyACM0',115200); s.write(b'OTA URL http://{ip}:{PORT}/carcluster.bin\\\\nOTA START\\\\n')\"")
print(f"[server] Ctrl-C to stop")

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[server] stopped")
