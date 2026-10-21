#!/usr/bin/env python3
"""Serve only RetroSync's CIA over LAN and generate its FBI QR code."""
import argparse
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
from pathlib import Path
import shutil
from urllib.parse import urlsplit
import qrcode


class CiaHandler(BaseHTTPRequestHandler):
    def __init__(self, *args, cia, **kwargs):
        self.cia = cia
        super().__init__(*args, **kwargs)

    def send_cia(self, body):
        if urlsplit(self.path).path != "/RetroSync3DS.cia":
            self.send_error(404)
            return
        # One allowlisted file; no directory listings or source/settings exposure.
        with self.cia.open("rb") as source:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(self.cia.stat().st_size))
            self.send_header("Content-Disposition", 'attachment; filename="RetroSync3DS.cia"')
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if body:
                shutil.copyfileobj(source, self.wfile, 65536)

    def do_GET(self):
        self.send_cia(True)

    def do_HEAD(self):
        self.send_cia(False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="This computer's LAN IPv4 address")
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    host = ipaddress.IPv4Address(args.host)
    if host.is_unspecified or host.is_multicast:
        parser.error("Use the actual LAN address, not 0.0.0.0 or a multicast address")
    root = Path(__file__).resolve().parent.parent
    cia = root / "dist/RetroSync3DS.cia"
    if not cia.is_file():
        parser.error("Build dist/RetroSync3DS.cia first")
    url = f"http://{host}:{args.port}/RetroSync3DS.cia"
    server = ThreadingHTTPServer((str(host), args.port), partial(CiaHandler, cia=cia))
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=10, border=4)
    qr.add_data(url)
    qr.make(fit=True)
    image = root / "dist/RetroSync3DS-FBI-QR.png"
    qr.make_image(fill_color="black", back_color="white").save(image)
    (root / "dist/RetroSync3DS-FBI-URL.txt").write_text(url + "\n")
    print(f"FBI > Remote Install > Scan QR Code\n{url}\nQR: {image}\nKeep this computer awake; Ctrl-C stops serving.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
