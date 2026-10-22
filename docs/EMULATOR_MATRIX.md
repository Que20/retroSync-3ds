# Emulator / format support

| Target | V1 behavior |
| --- | --- |
| GBA + open_agb_firm | Install one raw GBA or one GBA inside a bounded ZIP; track configured battery-save path/slot; two-way named-slot RomM sync |
| Raw emulator battery producers (e.g. verified mGBA/VBA output) | User explicitly verifies type/representation; SRAM/FLASH preserved, EEPROM converted in eight-byte blocks |
| open_agb_firm producer | User explicitly selects its byte representation; no unnecessary EEPROM swap |
| Other browsable platforms | Metadata/search/history only; no claim of console/emulator compatibility |
| RetroArch path/core discovery | Planned adapter; not wired into V1 installs/sync |
| Save states, unknown/compressed save containers | Unsupported; originals preserved |

Supported battery lengths: SRAM 32 KiB; FLASH 64/128 KiB; EEPROM 512 bytes/8 KiB. Length or file extension alone is
not format proof. ROM signatures can suggest SRAM/FLASH; EEPROM requires verified size. Explicit OAF type overrides
must agree with the selected type. Game identity, hacks, revisions and producer behavior still need verification.

Open_agb_firm does not support save states. This app does not launch an emulator automatically.
See [V1 behavior and recovery](V1.md) and [hardware acceptance](HARDWARE_TEST_PLAN.md).
