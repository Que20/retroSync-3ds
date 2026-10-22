# Physical New 3DS acceptance

These checks are **not yet executed on physical hardware**. Use copies of your progress and preserve originals.
Desktop and Azahar results are in [BUILD_VERIFICATION.md](BUILD_VERIFICATION.md).

## Installation and login

- Install the V1 CIA with FBI QR and from SD; upgrade over prototype 0.1.0 and verify HOME Menu launch/uninstall.
- Test New 3DS controls, touch row selection, software keyboard, cancel, back, search, paging, settings and exit.
- Enter URL/username/password, reboot and verify session reuse and renewal after access-token expiration.
- Test wrong credentials, insufficient scopes, HTTP opt-in, private CA, wrong hostname and console clock errors.
- Confirm sign-out removes main/temp/backup session files; passwords must never appear in settings or logs.

## Download and SD behavior

- Download representative raw and ZIP GBA ROMs; verify hashes, expected size and installed tracking after restart.
- Cancel during network transfer, extraction and hashing; never expose a partial playable ROM as installed.
- Exercise full SD, read-only/removal, missing directories and invalid/untracked destinations.
- Interrupt power before/after metadata flush, backup, rename and final record commit; preserve/recover evidence.
- Verify memory use, physical Wi-Fi/DNS/TLS throughput, timeout/cancellation latency, lid suspend/resume and low battery.

## Battery saves

- Run real open_agb_firm → RomM/producer → open_agb_firm round trips for SRAM 32 KiB, FLASH 64/128 KiB and both EEPROM sizes.
- Verify the selected game/region/revision, exact producer format, save chip and remote named slot before transfer.
- Test useSavesFolder both ways, slots 0–9 and per-game INI/type overrides. A changed mapping must require review.
- Cover first remote-only/local-only contact, equal bytes, local-only/remote-only changes, both changed and missing copies.
- Confirm conflicts default to Cancel, retain both copies and recheck choices if either side changed meanwhile.
- Race another device against upload; expect guarded HTTP 409 and preserved history, never a forced overwrite.
- Lose an upload response after server commit and before commit; neither case may produce a blind duplicate upload.
- Check backup retention/free-space behavior and restart recovery without deleting tracking files to bypass uncertainty.

Passing desktop tests or emulator screenshots does not establish physical SD durability or real-game save compatibility.
