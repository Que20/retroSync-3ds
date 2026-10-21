#!/usr/bin/env python3
"""Generate the committed QR for GitHub's latest CIA release asset."""
from pathlib import Path
import qrcode

URL = "https://github.com/Que20/retroSync-3ds/releases/latest/download/RetroSync3DS.cia"
destination = Path(__file__).resolve().parent.parent / "docs/assets/fbi-install.png"
destination.parent.mkdir(parents=True, exist_ok=True)
qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=8, border=4)
qr.add_data(URL)
qr.make(fit=True)
qr.make_image(fill_color="black", back_color="white").save(destination)
print(f"{destination}\n{URL}")
