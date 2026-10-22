#!/usr/bin/env python3
"""Prepare a virtual SD for synthetic screenshots, preserving existing settings."""
import argparse
from pathlib import Path

DEMO = """server=http://127.0.0.1:8765
token=rmm_mock_token
rom_directory=sdmc:/roms/gba
allow_http=true
"""

def prepare(sdmc):
    destination = sdmc / "3ds/RetroSync3DS/settings.ini"
    if destination.exists() and destination.read_text() != DEMO:
        raise ValueError("Existing RetroSync settings differ; use a separate emulator SD directory.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(DEMO)
    return destination

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdmc", type=Path, required=True, help="Emulator virtual SD root")
    args = parser.parse_args()
    try:
        destination = prepare(args.sdmc.expanduser())
    except ValueError as error:
        parser.error(str(error))
    print(f"Synthetic demo settings prepared at {destination}")
    print("Start scripts/mock_romm.py and load the release 3DSX in Azahar.")
