# Build and test verification

Updated 2026-10-22 for V1. Results below distinguish desktop services, the ARM application in an emulator, and
physical hardware. No physical New Nintendo 3DS was available.

## Reproducible build

Use `sh scripts/build-container.sh -j4` for the ARM build. CIA packaging additionally uses
`sh scripts/bootstrap-cia-tools.sh` and `make cia` in the same container, then `sh scripts/package.sh`.
The image is pinned to `devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`.

Components: devkitARM r68; libctru 2.7.0-1; citro2d 1.7.0-1; citro3d 1.7.1-2; curl 8.4.0-1;
mbedTLS 2.28.8-1; zlib 1.3.1-1. These pinned versions describe reproducibility, not a security-advisory clearance.
Desktop tests use system curl/OpenSSL; the ARM build uses the bundled mbedTLS path.

CIA title ID remains `000400000F7A6300`, with version 1.0.0 (1024), allowing an update over prototype version 16.
`verify-cia.py` verifies title/version, structure, native executable content, ExeFS/RomFS bounds and hashes, icon/banner
and bundled TLS roots. This cannot prove FBI installation or successful execution on hardware.

## Automated native-service tests

Run `make -f Makefile.host test` (C++17, libcurl, OpenSSL, zlib, Python 3 and pkg-config).
AddressSanitizer and UndefinedBehaviorSanitizer are enabled.

| Suite | Evidence |
| --- | --- |
| Portable core | 165 checks: paths/URLs, OAF slots, three-way conflict/deletion policy, all battery sizes and EEPROM golden-byte conversion, strict API parsing, settings recovery |
| HTTP/TLS | 24 checks: bearer auth, response redaction, no redirects, memory cap, cancellation, CA and hostname validation |
| Library integration | 54 checks: password login/form encoding, refresh rotation, adaptive 20/5/1 pages, streamed ZIP install, persistent identity/paths, slot binding, two-way sync, stale conflict choices, guarded 409 and uncertain-upload recovery without blind retry |
| Storage recovery | 29 checks: reference digests, bounded reads, retained backups, changed/untracked destination protection, interrupted rename recovery and hostile journal rejection |
| ZIP safety | 29 checks: stored/deflated members, traversal, symlinks, duplicate games, truncation, CRC/encryption/size limits, cancellation and existing-file preservation |
| Format HTTP round trips | SRAM 32 KiB, FLASH 64/128 KiB, EEPROM 512 bytes/8 KiB; nonuniform bytes verify both conversion directions through the actual service and HTTP path |

The synthetic fixture intentionally ties save timestamps to exercise the numeric-ID tie-breaker. Tests use generated
homebrew-shaped ROM headers and synthetic battery bytes; no commercial game or real credential is in the repository.

## Real RomM 5.4.0 server

A user-authorized deployment was tested over validating HTTPS. Its address/account/credentials and raw metadata are
kept outside the repository and omitted here.

- Username/password login and session creation passed with the native HTTP service.
- Adaptive catalog reads passed on all 19 platforms sampled, 32 pages in total; the previous oversized GBA page now
  falls back from 20 to five items rather than failing.
- A real GBA ZIP was downloaded, verified, extracted and tracked successfully.
- A uniquely named temporary slot with synthetic bytes passed initial/incremental uploads, remote-to-local download,
  conflict detection/cancellation and an explicit Keep local upload. Existing history was retained.
- The test exposed tied-timestamp ambiguity in the summary endpoint; current-slot selection now matches RomM's
  `(updated_at DESC, id DESC)` guarded-write order.
- Created test rows/device were removed and the pre-existing save IDs, hashes and update times were checked unchanged.

## ARM / Azahar

Azahar 2126.2 official Apple Silicon build is installed only in the ignored `build-host/azahar/` directory.
Its official archive SHA256 is `c24dec2bd5111b7d79513d686c5aece6612cdbf9737b2e6ea99cfc426f1d4aef`.

The actual ARM `.3dsx` boots with the new citro2d UI. Username/password entry, real HTTPS login, 19 platforms,
adaptive GBA browsing, search and a complete real ZIP download/install passed in Azahar. The installation's SHA256
was independently recomputed on the host and matched the persisted ARM manifest. Restart recovered the saved session
and displayed the tracked game as ON SD. No password field is stored in the saved configuration.

The ARM UI additionally passed a real isolated-slot multipart upload, remote-to-local update, conflict detection,
default Cancel (both copies unchanged), and Keep remote with a local backup. Test data/device were removed and original
save IDs/hashes/update times checked unchanged. The native CIA version 1024 passes structure/content checks; the ZIP
and CIA-only development server pass 12 additional packaging/integrity checks. Public screenshots use only synthetic
data from `scripts/mock_romm.py`; native PNG captures are copied without image editing.

## Published release verification

[Version 1.0.0](https://github.com/Que20/retroSync-3ds/releases/tag/v1.0.0) was built by GitHub Actions after
both desktop and ARM jobs passed. The public CIA and ZIP were downloaded again: standalone and per-file SHA256
checks passed, the bundled CIA matched the standalone CIA, and the native CIA verifier accepted title/version,
executable, icon/banner and TLS roots. The README QR was decoded and its `latest/download` response matched that CIA.
Private test credentials were absent from the public archive. The full desktop suite also passed locally on macOS
and Linux with address/undefined-behavior sanitizers. TLS fixtures use a CA/leaf chain and explicit HTTP body lengths.

## Remaining hardware acceptance

FBI QR installation, HOME Menu launch, physical Wi-Fi/TLS/clock behavior, real SD power-loss/read-only/full-card cases,
suspend/resume, cancellation latency, memory pressure and real-game SRAM/FLASH/EEPROM round trips still require a
physical console. See [HARDWARE_TEST_PLAN.md](HARDWARE_TEST_PLAN.md). Emulator and desktop results do not replace these.
