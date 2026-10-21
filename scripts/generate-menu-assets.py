#!/usr/bin/env python3
"""Generate original, simple HOME Menu graphics and a silent banner sound."""
from pathlib import Path
import wave
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent.parent / "assets"
root.mkdir(exist_ok=True)
icon = Image.new("RGB", (48, 48), "#102132")
draw = ImageDraw.Draw(icon)
draw.rounded_rectangle((3, 3, 44, 44), radius=9, fill="#18384a", outline="#3dd9b1", width=2)
draw.arc((10, 10, 37, 37), 200, 345, fill="#3dd9b1", width=4)
draw.polygon([(37, 12), (39, 24), (29, 19)], fill="#3dd9b1")
draw.arc((10, 10, 37, 37), 20, 165, fill="#e4f3fc", width=4)
draw.polygon([(10, 35), (9, 24), (19, 29)], fill="#e4f3fc")
icon.save(root / "icon.png")
banner = Image.new("RGB", (256, 128), "#102132")
banner.paste(icon.resize((72, 72), Image.Resampling.NEAREST), (14, 28))
draw = ImageDraw.Draw(banner)
font = ImageFont.load_default(size=20)
small = ImageFont.load_default(size=12)
draw.text((96, 35), "RetroSync", font=font, fill="#e4f3fc")
draw.text((96, 61), "3DS", font=font, fill="#3dd9b1")
draw.text((96, 91), "RomM client", font=small, fill="#a9bfd0")
banner.save(root / "banner.png")
with wave.open(str(root / "silence.wav"), "wb") as sound:
    sound.setnchannels(1)
    sound.setsampwidth(2)
    sound.setframerate(22050)
    sound.writeframes(bytes(2205 * 2))
