# RetroSync 3DS task checklist

Updated 2026-10-21. Checked means implemented/inspected at the stated validation level, not assumed on-device success.

## 1 — Research and specification

- [x] Inspect pinned Grout source: browse/search, mappings, negotiate/discovery, conflicts and history.
- [x] Inspect current RomM source and docs, compare 4.9.0 and 5.4.0 tagged device/save protocols.
- [x] Record methods, fields, scopes, references and capability/fallback policy in API_COMPATIBILITY.md.
- [x] Verify OAF global/per-game config paths, save slots, byte-order issue and file lengths in source.
- [x] Document adapter matrix, architecture, risks, persistence/recovery design and incremental release gates.
- [ ] Identify the user's deployment version and validate its exported OpenAPI against a release profile.

## 2 — Minimal compiling 3DS prototype (0.1.0 deliverable)

- [x] Build devkitARM/libctru application with dual-screen console UI and metadata.
- [x] D-pad/A/B, SELECT settings, L/R page navigation, touch list/action targets.
- [x] Editable URL/token/ROM path and explicit HTTP opt-in; save/test/disconnect.
- [x] Mask token display; persistent settings temp/flush/backup with recovery.
- [x] Worker-thread Wi-Fi/API requests, bounded memory/timeouts and cancel flag.
- [x] Validating TLS with bundled/public or custom CA; no redirect forwarding.
- [x] Authenticated platforms, 20-item ROM pagination/search, stable displayed IDs.
- [x] Read-only save metadata/history; exact null vs string slot representation.
- [x] SD free-space display; OAF global config/default status.
- [x] Sanitized metadata and errors; no credential/body logging.
- [x] Cross-compile `.3dsx`, SMDH and embedded CA RomFS with official toolchain.
- [x] Desktop ASan/UBSan core tests and mock HTTP/HTTPS tests.
- [x] Reproducible container/host scripts, CI definition, install ZIP and documentation.
- [ ] Boot/run on real New 3DS; measure memory/cancellation and validate UI layout/input.
- [ ] Test real RomM 4.9.0 and 5.4.0 authenticated browse over 3DS Wi-Fi/TLS.

## 3 — GBA MVP (next implementation milestone; not delivered yet)

- [x] Pure path/INI helpers with slot 0–9 tests.
- [x] Pure canonical three-way reconciliation and deletion/unknown cases tested.
- [x] Explicit-format EEPROM conversion with golden bytes and round-trip tests.
- [ ] Versioned install/save manifest, server/account identity, stable device ID.
- [ ] Adapter interface, per-game INI override/type provenance and collision-safe installed naming.
- [ ] ROM detail/file selection; streamed `.gba` download, expected size/hash, SD preflight, progress/cancel/retry.
- [ ] Same-directory staging, flushed backups, durable journal and restart recovery before enabling replacements.
- [ ] Remote format/core metadata validation; immutable original save storage and canonical hashes.
- [ ] `Download ROM + saves` using explicit active slot choice and verified conversion.
- [ ] Source-verified device registration/negotiate/ack/complete implementation and safe older-server fallback.
- [ ] Two-way uploads/downloads; guarded concurrency, idempotent retry evidence and audit logs.
- [ ] Standard conflict modal with exactly Keep local / Keep remote / Cancel and complete copy metadata.
- [ ] Real EmulatorJS → OAF → EmulatorJS round trip for SRAM, FLASH and both EEPROM sizes.

Release gate: do not call this a GBA sync MVP until backup/recovery, conflict cancellation, concurrent remote change,
lost upload response and real on-device round trip have passed. Helpers alone do not meet the gate.

## 4 — Hardening

- [ ] Test interrupted transfer at every journal stage; full SD/read-only/removal cases.
- [ ] Safe read retries/backoff; no duplicate write retry after lost response.
- [ ] Large catalogs, oversized/hostile metadata, clock skew, timeout/DNS behavior.
- [ ] Session completion confirmation and local/server baseline convergence.
- [ ] UI log viewer and bounded redacted audit rotation.
- [ ] Full keyed English strings and UTF-8/font preparation for French.
- [ ] Review toolchain/port-library security updates before public release.

## 5 — Expansion

- [ ] RetroArch config/core discovery and adapter with user overrides.
- [ ] Verified RetroArch battery formats; state tuple metadata/explicit acknowledgement.
- [ ] Bulk sync with per-conflict prompts and confirmed/failed/skipped summary.
- [ ] Optional lazy small-cover cache with decode size limits.
- [ ] Additional emulator adapters and explicit BIOS/archive/multi-file capability notices.
- [x] CIA metadata, original icon/banner, pinned packaging tools, native CIA build and structural/hash verification.
- [x] LAN CIA-only server and direct-URL FBI QR generation.
- [ ] Actual FBI QR install, HOME launch, networking and uninstall validation on New 3DS.

See BUILD_VERIFICATION.md for results from this environment and HARDWARE_TEST_PLAN.md for unexecuted acceptance steps.
