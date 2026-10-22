# V1 checklist

Software validation and physical-console validation are tracked separately.

- [x] Fix oversized real catalog pages with adaptive 20/5/1 pagination and actual-count navigation.
- [x] Replace manual token entry with username/password login and persisted rotating sessions.
- [x] Scope installs/baselines/device identity to verified server/account identity.
- [x] Stream and verify GBA/ZIP downloads; reject unsafe/ambiguous archives.
- [x] Persist install records and recover interrupted staged installs/replacements.
- [x] Track local battery changes and OAF global/per-game save paths/slots/type overrides.
- [x] Explicit SRAM/FLASH/EEPROM type/producer selection and byte conversion.
- [x] Two-way guarded sync, preserved history, backup copies and Cancel-default conflicts.
- [x] Reject changed preconditions, unknown upload outcomes and automatic deletion.
- [x] Exercise native HTTP services against synthetic fixtures, including all five battery lengths.
- [x] Validate real RomM 5.4.0 login, catalog, download, two-way sync and conflicts in an isolated test slot.
- [x] Confirm existing real-server save metadata unchanged and remove created test data/device.
- [x] Native graphical dual-screen UI; remove redundant screenshot guide.
- [x] Complete emulator input/layout/end-to-end transfer acceptance, including actual ARM live-server save transfers.
- [ ] Final native CIA, packaging, CI and public release checks.
- [ ] Physical New 3DS: FBI install/HOME boot, Wi-Fi/TLS, SD errors, suspend/resume and cancellation timing.
- [ ] Real gameplay round trips through open_agb_firm and the remote producer for all supported battery types.

Expansion: RetroArch adapters, other ROM formats/platform installs, browser-only OIDC login, offline library cache,
cover artwork, bulk sync, negotiated sync sessions and bounded backup-retention controls.
