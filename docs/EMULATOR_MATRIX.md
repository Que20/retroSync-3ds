# Emulator paths and format matrix

The 0.1.0 application reads open_agb_firm global config for display and lists remote metadata only.
All save file conversion APIs are portable, unit-tested helpers; none writes a user's save.

## open_agb_firm

Verified against `401c3dff900f658d3edf77ababcfaf5dca54a0a8`:

| Setting / artifact | Verified rule |
| --- | --- |
| Work directory | `sdmc:/3ds/open_agb_firm` |
| Global config | `config.ini`, `[general] useSavesFolder`, default `true` |
| Shared save directory | Work directory + `/saves`; no alternate directory setting in this revision |
| `useSavesFolder=false` | Save and per-game config beside ROM |
| Per-game config | Same ROM stem with `.ini`; located in selected save directory |
| `[game] saveSlot=0` | `Game.gba` → `Game.sav` |
| Slots 1–9 | `Game.gba` → `Game.1.sav` … `Game.9.sav` |
| Save type | Global defaults/database plus per-game `[game] saveType`; ROM hacks/patches may override |
| ROM | Uncompressed playable `.gba`; extraction not implemented by RetroSync |
| States | Unsupported; no state operation offered |

Sources: [configuration/defaults/parser](https://github.com/profi200/open_agb_firm/blob/401c3dff900f658d3edf77ababcfaf5dca54a0a8/source/arm11/config.c),
[paths and per-game load](https://github.com/profi200/open_agb_firm/blob/401c3dff900f658d3edf77ababcfaf5dca54a0a8/source/arm11/open_agb_firm.c#L212),
[path constants](https://github.com/profi200/open_agb_firm/blob/401c3dff900f658d3edf77ababcfaf5dca54a0a8/include/arm11/config.h),
[README limitations](https://github.com/profi200/open_agb_firm/blob/401c3dff900f658d3edf77ababcfaf5dca54a0a8/README.md).

RetroSync permits a configurable ROM directory (default `sdmc:/roms/gba`). The current build does not create that
directory because it has no download operation. An adapter override for a future/nonstandard OAF work directory
must be explicit; do not invent a `savesPath` config key.

## Battery data validation and conversion

| Known save type | Expected raw bytes | Emulator raw ↔ OAF | Current policy |
| --- | --- | --- | --- |
| SRAM 256 kbit | 32768 | Copy if producer/type verified | Pure helper tested |
| FLASH 512 kbit | 65536 | Copy if producer/type verified | Pure helper tested |
| FLASH 1 Mbit | 131072 | Copy if producer/type verified | Pure helper tested |
| Small EEPROM | 512 | Reverse bytes within each 8-byte block when encoding differs | Pure helper/golden bytes/round-trip tested |
| Large EEPROM | 8192 | Same 8-byte reversal | Pure helper/round-trip tested |
| Unknown, padded, compressed, wrapped or RTC-appended | Uncertain | No guessing, stripping or truncation | Reject and preserve original |

OAF's vendored libn3ds revision is `ccbb6a94d60bea2e6a1901263251636b192bad3f`.
Its [save-size lookup](https://github.com/profi200/libn3ds/blob/ccbb6a94d60bea2e6a1901263251636b192bad3f/source/arm9/drivers/lgy9.c#L53)
uses shifts 9/13 for EEPROM, 16/17 for FLASH and 15 for SRAM, with optional override. OAF names its small EEPROM
mode `eeprom_8k`; the actual file size in that lookup is 512 bytes. Do not infer file length from that symbolic name.

OAF explicitly documents incompatible emulator EEPROM exports and links
[exelotl's fixer](https://github.com/exelotl/gba-eeprom-save-fix/blob/fdc61d84c6b7779dd39786a70ee98739c9568001/index.html#L45),
which reverses each group of eight bytes. RetroSync implements that elementary transformation independently.
It is self-inverse. Raw contents generally do **not** identify which byte order they use; the helper requires a
known source and destination encoding, and never guesses from `.srm`/`.sav`. Synthetic tests do not establish
compatibility for a particular EmulatorJS core or a commercial/hacked game.

A future download must tie type/provenance to a verified ROM hash and producer/core version (including save chip
overrides), check exact length, reject obvious ZIP/gzip containers, preserve the original, then materialize `.sav`.
There is no universal magic number/checksum proving that arbitrary battery bytes belong to a particular game.
All-FF/all-zero saves can be valid empty memory; length and content checks cannot prove authenticity.
EmulatorJS is a frontend for multiple cores; "EmulatorJS GBA .srm" alone is insufficient metadata.

## RetroArch (phase 2, not implemented)

Read the user's actual `retroarch.cfg` and overrides, starting from candidate `sdmc:/retroarch/retroarch.cfg` only as
a discovery hint. User-configurable save/state roots are mandatory; do not hardcode universal directories.
Resolve `savefile_directory`, `savestate_directory`, content-directory flags, sort-by-core/content flags and the
loaded core. Battery files typically use the content stem + `.srm`; numbered states use `.state`, `.state1`, etc.
These are discovery hypotheses until verified against the selected 3DS build/core and config. Native WebDAV/cloud
sync availability on that build has not been verified. Consult the user's settings, never silently rewrite them.

Record ROM hash, emulator, core identifier, core build/version and relevant state format/compression metadata.
Battery transfer can be offered only when formats agree. States require a verified compatible tuple or explicit
acknowledgement with backup; do not infer compatibility from `.state` or a shared core name. OAF states remain disabled.

For the transport comparison and verified RomM WebDAV route, see `API_COMPATIBILITY.md`.
