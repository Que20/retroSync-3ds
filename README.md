# RetroSync 3DS

## What is it?

RetroSync 3DS brings your self-hosted [RomM](https://romm.app/) library to a **New Nintendo 3DS**.
V1 focuses on downloading GBA games and keeping their battery saves in sync with **open_agb_firm**.

## What does it do?

- Browse and search your platforms and games with a native dual-screen interface.
- Sign in with your **username and password**. No API token to type; sessions renew automatically.
- Download a GBA ROM, or a ZIP containing one GBA ROM, and verify it before installation.
- Track installed games and local save changes separately for each RomM account.
- Sync a chosen battery-save slot in both directions, with **Keep local / Keep remote / Cancel** when both copies changed.
- Keep backup copies before replacing saves, and preserve RomM version history.

All platforms can be browsed. Game installation and save conversion in V1 target GBA/open_agb_firm.
Save states and automatic game launching are not supported.

![Game actions](docs/screenshots/game.png) ![Save conflict](docs/screenshots/conflict.png)

Native Azahar captures using a synthetic demo library.

## Installation

You need a New Nintendo 3DS with **Luma3DS and FBI**.

### QR code

Open **FBI → Remote Install → Scan QR Code**, then scan:

![Install RetroSync 3DS with FBI](docs/assets/fbi-install.png)

This installs the [latest CIA](https://github.com/Que20/retroSync-3ds/releases/latest/download/RetroSync3DS.cia)
from GitHub. No local server is needed.

### USB / SD card

1. Download [RetroSync3DS.cia](https://github.com/Que20/retroSync-3ds/releases/latest/download/RetroSync3DS.cia).
2. Power off the console and connect its SD card using a **USB SD card reader**.
3. Copy the CIA into `/cias/`, put the card back, then use **FBI → SD → cias → RetroSync3DS.cia → Install CIA**.

Launch RetroSync from the HOME Menu. A Homebrew Launcher `.3dsx` is also included in the release ZIP.

### First launch

1. Open **Settings** with SELECT. Enter your server URL, username and password, then choose **Sign in**.
2. Choose a GBA game and **Download game**. Files are stored in `/roms/gba/` by default, with stable names to avoid collisions.
3. Open the installed game in open_agb_firm. When finished playing, return to RetroSync.
4. Choose **Remote save history** to link an existing named slot, or **Create a new save link**.
5. Confirm the game's save-chip type and remote byte format (new links default to mGBA/EmulatorJS), then **Link and sync**.
   EEPROM requires its verified size (512 bytes or 8 KiB); size alone does not identify a format.
6. Use **Sync saves** before and after playing. If both copies changed, choose which progress to keep or cancel.

D-pad selects; A opens; B returns or cancels a transfer; L/R changes game pages; Y searches; START exits.
Tap a selected row to open it. RetroSync reads open_agb_firm's configured save folder and per-game slot.

## Notes

- Save writes target **RomM 5.4.0**. The account needs library, asset and device read/write permissions; no admin privileges are required.
- HTTPS validates the certificate and hostname. Passwords are not saved; session credentials are readable by someone with the SD card.
- Original save bytes and replacement backups are retained. An uncertain upload is checked against server history before another write; deletion is never synchronized automatically.
- For battery-save format limits, recovery and test coverage, see the [V1 guide](docs/V1.md).
- Real-server integration is tested; physical-console/FBI and real gameplay round trips remain a hardware acceptance step.
- No games, BIOS files, saves or credentials are distributed. Licensed under [MIT](LICENSE); see [dependency notices](THIRD_PARTY.md).

## Links

- [Downloads and releases](https://github.com/Que20/retroSync-3ds/releases)
- [RomM documentation](https://docs.romm.app/latest/) · [open_agb_firm setup](https://github.com/profi200/open_agb_firm#setup)
- [V1 guide](docs/V1.md) · [Build and test results](docs/BUILD_VERIFICATION.md)
- [Hardware acceptance](docs/HARDWARE_TEST_PLAN.md) · [API reference notes](docs/API_COMPATIBILITY.md)
