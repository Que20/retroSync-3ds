# Hardware and real-server acceptance plan

**Not executed**: no New 3DS or authenticated target RomM was available.
Record console model, Luma version, homebrew launch method, SD filesystem/free space, app artifact SHA256,
RomM release/commit/OpenAPI profile, reverse proxy, OAF revision and core versions with every run.
Use legal homebrew/test ROMs and disposable test save copies. Preserve originals outside the console before mutation tests.

## Current prototype acceptance

For the CIA path, also run FBI → Remote Install → Scan QR Code with the local server active. Verify name/title ID,
SD installation, HOME icon/banner, launch/exit/relaunch, settings keyboard, CA RomFS access, authenticated browsing,
reboot, same-ID update and uninstall. These CIA hardware steps are still unexecuted.

1. Copy `3ds/RetroSync3DS` from the ZIP onto SD. Launch `.3dsx` through Homebrew Launcher.
   Confirm both screens draw without crashes and START exits. Do not infer a successful boot from compilation alone.
2. With Wi-Fi disconnected, open SELECT → Test connection. Expect a useful Wi-Fi error and responsive UI.
3. Enter the final HTTPS base URL (no trailing slash), a scoped token (`platforms.read`,`roms.read`,`assets.read`),
   save settings, then test. Confirm the authenticated platform list, sorted names, no token displayed or logged.
4. Test invalid/revoked token → 401, missing scope → 403, reverse proxy login redirect → explicit redirect refusal.
   A successful public heartbeat must not report an authenticated connection without platform success.
5. Test trusted public CA, unknown CA, wrong hostname and expired certificate; the latter three must fail.
   Set the console clock incorrectly to verify explicit TLS failure; restore the clock afterward.
   For a private CA, copy its PEM root bundle to `/3ds/RetroSync3DS/cacert.pem`; verify no bypass switch is required.
6. Browse a platform with 45+ ROMs: D-pad and touch select; A opens; L/R fetch 20-item pages;
   search Y includes spaces, punctuation and accented input; B returns. Empty and permission-hidden platforms are graceful.
   Current console metadata replaces non-ASCII bytes; record this known limitation separately from API search behavior.
7. Select a ROM and press X. Confirm history filename, save ID, producer, size, timestamp and exact slot/null status.
   No historical row may be restored automatically. No save/state/ROM is written by this build.
8. While requesting a slow page, press B and START in separate runs. Verify UI remains responsive, cancellation/exit
   finishes, sockets close and repeated connect/cancel cycles do not leak heap. Measure DNS/connect worst cases.
9. Load OAF config with useSavesFolder true/false and malformed values. Expect the correct status or mapping-disabled warning.
   The prototype does not read per-game config into actions because transfers do not exist yet.
10. Save settings twice, interrupt an SD copy of the primary settings file on a test card, then restart: a valid `.bak`
    must recover. Disconnect must erase primary/temp/backup token files, including `.bak.tmp`; also revoke it in RomM.
11. Test touch targets, repeated key input, long metadata, empty lists and every status/error wrap on both screens.
    Confirm SD free-space display against a desktop reading. Current console buttons are text hit areas, not final graphics.

Run against tagged 4.9.0 and 5.4.0 separately. For a quick unauthenticated-to-production substitute, use
`python3 scripts/mock_romm.py --bind <desktop LAN IP> --port 8765`, configure `http://<desktop LAN IP>:8765`,
token `rmm_mock_token`, and explicitly enable HTTP. This is synthetic metadata only, not an HTTPS or live-RomM acceptance result.

## Required GBA acceptance after milestone 3 exists

These steps **cannot be performed by 0.1.0**. They are the release gate for the planned sync MVP.

1. Register device once; restart twice; verify the same server/account-scoped UUID and no duplicate registration.
2. Use separate homebrew fixtures for SRAM 32 KiB, FLASH 64/128 KiB, EEPROM 512 B/8 KiB. Tie ROM hashes to verified
   chip metadata and record actual EmulatorJS core/build. Include a patched-game override case and an unknown-type case.
3. Create recognizable progress in EmulatorJS, close the player, inspect the exact active slot in RomM and retain original
   bytes/hash. Choose Download ROM + saves. Verify `.gba` checksum, installed identity manifest and OAF `.sav` name/length.
4. Launch OAF manually. Confirm progress loads; play and save new recognizable progress. Fully return/reboot to homebrew
   after OAF writes its battery data. Launch RetroSync after playing; sync; inspect a new remote version and prior history.
5. Load the newly synced battery save in EmulatorJS and confirm the new progress. For EEPROM, compare canonical hashes
   and verify raw bytes reverse in groups of eight both directions; no double conversion on a repeated sync.
6. Repeat with useSavesFolder false, shared-folder true, slots 0/1/9 and per-game INI override. Use same-title region/revision
   duplicates and renamed server files; the installed RomM ID mapping must remain stable and no save path may collide.
7. Establish a baseline, change local only → upload; remote only → download; neither → unchanged/no upload; both → conflict.
   Deliberately skew clocks by ±48 hours and repeat. Different first-contact copies must still conflict.
8. The conflict modal shows game/slot/timestamp/size/producer. Test each exact choice: Keep local, Keep remote, Cancel.
   Confirm losing-version backup/history; Cancel changes neither side and reports no successful sync.
9. Delete a local or remote copy after baseline: show intervention notice and do not silently delete the survivor.
   Test multiple active slots plus historical/null-slot saves; no history row is mistaken for active progress.
10. Simulate Wi-Fi loss during body, after upload server commit but before response, after local replacement before ack,
    and after complete request before response. Restart. Journal must reconcile evidence, preserve backups and avoid duplicates.
11. Mutate remote slot while 3DS transfer is staged: guarded upload fails with 409 or precondition recheck returns to conflict.
    Test older-server fallback: without a reliable write guard, writes stay unavailable and explain why.
12. Full SD, read-only card, forced termination at each journal phase, malformed hash/size, traversal filenames, ZIP bombs,
    unsupported archive/multi-file game and unavailable BIOS all fail without replacing a good local file.
13. OAF state actions remain disabled. Wrong/unknown RetroArch core version cannot silently restore a state.
14. Final success requires local manifest + remote bytes/ack + session response agreement. A missing response is unconfirmed.

## Report format

For each case record expected/actual, PASS/FAIL/BLOCKED, artifact hash, logs with secrets removed and before/after file
hashes. Keep screenshots of conflict choices and error rendering. Label mock, emulator and real-console evidence distinctly.
