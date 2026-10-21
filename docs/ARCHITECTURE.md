# Architecture and implementation plan

## Delivered milestone

0.1.0 is a small **read-only RomM browser**, cross-compiled as a real ARM `.3dsx`.
It does not install ROMs or sync progress yet. The portable safety functions are tested building blocks,
not evidence that an end-to-end transfer works. No connected server, API credential or real console was supplied.

| Layer | Present implementation | Next implementation |
| --- | --- | --- |
| UI | libctru top metadata/status + bottom console lists, D-pad, A/B, L/R, SELECT, keyboard and touch hit areas | Per-game action menu, conflict modal, richer text renderer, eventual small cover cache |
| Network | libcurl over SOC, validated TLS, same configured base, no redirects/proxy inheritance, one cancellable worker | Stream-to-file body sink, transfer progress, classified retry/backoff |
| API/catalog | Strict bounded cJSON parser, heartbeat, platforms, 20-ROM pages/search, save metadata/history | ROM detail/files, scoped capability profiles, verified device/session API |
| Adapters | Portable open_agb_firm INI parser and save-path mapper; application reads global config only | Adapter interface, per-game INI overrides, save metadata provenance; RetroArch config detection |
| Save formats | Explicit type/encoding/length validation, conservative container rejection, reversible EEPROM 8-byte swaps | Verified core/type mapping and actual legal homebrew format corpus |
| Reconciliation | Pure hash/baseline planner; no time-based winner | Persistent per-representation baselines, remote precondition rechecks and conflict decisions |
| Persistence | Settings temp/flush + prior backup, startup fallback; explicit local disconnect | Versioned install manifest, transaction journal, backup retention UI |
| Logging | User-visible status; no request/response or credential logging | Bounded sanitized audit log and log-view screen |

Current modules: `source/main.cpp`, `source/core/{http,api,core}.cpp`, public interfaces in `include/retrosync/`.
Keep the UI thin as download/sync work is added; move its worker/job ownership into a service before introducing new mutations.
Use C++17 without RTTI/exceptions on 3DS, C for the vendored JSON library. Heavy desktop GUI dependencies are avoided.

## Thread and memory ownership

The UI owns settings, the currently displayed lists and one job. A job receives a snapshot of settings and a fixed
API path. The worker owns response parsing and publishes completion with release/acquire atomics. The UI joins it
before reading results or reclaiming memory. Only atomic cancel/progress fields are shared while it runs.
No UI or GPU calls occur in the network worker; no network request runs on the UI thread.

Limits: 1 MiB aligned SOC buffer, 128 KiB worker stack, 512 KiB JSON body, depth 64,
512 platforms/save-history rows, 20 ROMs, 1 KiB retained summary per ROM. cJSON's temporary tree uses additional heap
proportional to the bounded body, then is freed. Do not read ROM files into this buffer. Cover decoding is absent.
Requests have 8-second connect, 25-second transfer and 10-second low-throughput limits; B sets cancellation.
Connect runs two requests, so its total can approach 50 seconds without UI blocking. Exit cancels and joins.
Cancellation during a blocking resolver/SDK operation depends on libctru/curl returning; hardware timeout behavior
must be measured. Do not claim hard real-time cancellation from the desktop tests.

The INI read buffer is heap-allocated, avoiding a 64 KiB allocation on the small main-thread stack.
Shutdown order: stop worker, close AC/curl, SOC, socket buffer, RomFS, graphics.

## Adapter contract for the GBA milestone

Implement an adapter interface with `capabilities`, `resolveRomDestination`, `resolveSaveSlots`, `inspectSave`,
`canonicalize` and `materialize`. Each returns either a verified mapping or an explanation. It must not mutate files.
Capabilities distinguish ROM archive support, battery saves and states. `open_agb_firm.states=false` always.
No arbitrary server-provided path or state compatibility flag is trusted.

For open_agb_firm, load global `/3ds/open_agb_firm/config.ini`, then the ROM's `.ini` (resolved using the global
`useSavesFolder` convention). Apply `[game] saveSlot` and verified save type overrides. Do not silently change an
existing open_agb_firm config. The present UI only reports global config/defaults; per-game mapping is not wired yet.
Filename collisions in the shared save folder must be prevented when installing, e.g. append stable RomM ID to
the installed ROM stem, and remember the result permanently. Do not rederive installed names from mutable titles.

## Manifest design (not yet implemented)

Versioned JSON documents are scoped to an explicitly identified server and account. A ROM entry stores RomM ID,
selected ROM file IDs, advertised hashes, verified installed hash, local path, platform ID, adapter and content/core
metadata. A save entry stores the exact server slot string (or archival null), local slot, selected remote ID,
local raw hash, remote raw hash, canonical hash, type/encoding provenance and last successful transaction ID.
Device ID is scoped to this server/account, never globally reused. Do not infer account identity from hostname alone.
Renames update display metadata; a changed ROM hash triggers revalidation. Duplicate titles/regions/revisions/hacks
remain distinct. A matching filename is not identity proof. Existing local files need hash-based import/mapping.

## Three-way decision policy

`reconcile()` assumes comparable canonical hashes. It ignores timestamps. Unknown hashes/format are unsupported,
not equal. No baseline and two different existing copies produces conflict, even if timestamps differ.

| Local / remote vs successful baseline | Decision |
| --- | --- |
| Identical to each other | Unchanged |
| Local unchanged, remote differs | Download candidate |
| Remote unchanged, local differs | Upload candidate |
| Both differ | Conflict |
| Only local exists, no prior copy | Upload candidate |
| Only remote exists, no prior copy | Download candidate |
| Prior copy existed, one side disappears | LocalDeleted / RemoteDeleted; user intervention, no propagation |
| Both disappeared after baseline | BothDeleted; keep manifest/audit tombstone, no mutation |
| Unknown content hash, save type or encoding | Unsupported; preserve both |

"Candidate" still requires format validation, backups and a fresh remote/local precondition check.
The server plan is input, not authorization to override this policy. Slots/history are explicit: never select a
historical save just because it is the first array entry, and never collapse archival null into `autosave`.
For an established active slot, identify its current row within that slot, fetch/hash the exact bytes, and detect
a changed row/hash before commit. If row ordering is ambiguous, ask for selection.

## Transfer journal and replacement design (not implemented)

For every write record: operation UUID, ROM ID, exact slot, remote ID, expected old hashes, intended new hashes,
local temp/backup paths, server profile, and phase. Phases: planned → staged → validated → backed_up → local_committed
→ server_confirmed → manifest_committed → session_confirmed. Recovery inspects evidence; it never assumes the last
request succeeded just because it was sent.

Download into a same-directory `.part` file in fixed chunks (target 32 KiB). Check SD free space including ROM,
temporary copies, conversion output and backup. Hash while streaming; validate expected size and authoritative
hash when available. Do not resume without a verified Range/ETag contract. Reject multi-file/archive GBA inputs
until extraction paths, compressed/uncompressed limits and `.gba` validity are implemented.

Preserve the losing file in an operation-specific backup directory and flush it before replacement. **libctru's
`rename` implementation may delete an existing destination before moving the temp file**, so POSIX atomic overwrite
cannot be assumed. Use the journal and retained backup for recovery. Flush content and persist phase changes; FAT/SD
power-loss durability must be tested. Do not remove a backup until a documented retention decision is made.
See [archive_rename/archive_fsync source](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/source/archive_dev.c#L926).

Upload canonical bytes appropriate for the server producer, retaining the source raw file and previous remote row.
Use guarded slot uploads without autocleanup. A lost response triggers a query for matching content before any retry.
Never overwrite an arbitrary remote row with PUT. Complete the session only after local manifest and server evidence
agree. A completion timeout is "unconfirmed", not success. Bounded exponential backoff applies to safe reads only
until write idempotency is implemented.

Conflict modal must show game, **exact slot**, local/remote timestamps, sizes, producer and hashes, with exactly:

1. `Keep local` — preserve remote history and upload local copy.
2. `Keep remote` — back up local copy and download remote copy.
3. `Cancel` — leave both unchanged.

Default focus is Cancel. Bulk sync queues the same per-conflict decisions and ends with confirmed/skipped/failed
counts. Never resolve from "newer" alone. This modal is a required next-milestone UI, not present in 0.1.0.

## Security, localization and packaging

Tokens are masked in keyboard/display; saved settings and `.bak` are plaintext on SD, protected only by possession.
Do not log tokens, full URLs, headers or response bodies. Clear current, temporary and backup settings on local
disconnect and advise remote revocation. Secure erasure on flash cannot be promised. No credentials are committed
or packaged. HTTP LAN opt-in is explicit; TLS validation cannot be disabled.

English navigation labels begin in `strings.hpp`; remaining inline strings must be moved to keyed locale tables
before adding French. Current console metadata intentionally converts control/non-ASCII bytes to spaces; full accented
French text needs a UTF-8 font renderer. This is localization groundwork, not French support.

Packages: Homebrew Launcher `.3dsx` and native `.cia` built from the same ARM ELF with embedded RomFS. CIA packaging
now has an explicit RSF, SD-only filesystem access, service/dependency list, original icon/banner and pinned makerom/
bannertool build. Structural/hash checks pass; real FBI installation, HOME launch and uninstall remain untested.
See CIA_PACKAGING.md. This is not a 3DSX forwarder.

## Risk register

| Risk | Mitigation / remaining verification |
| --- | --- |
| Deployment differs from upstream latest | Pinned references; strict parsers; tested release profiles before writes |
| Old protocol relies on timestamps | Local canonical three-way planner; inspect raw remote bytes |
| EEPROM length/type is ambiguous | Explicit verified metadata; no extension/size-only inference |
| Patched games use a different save chip | Honor per-game config/verified database; unsupported by default |
| Remote mutation between read and write | Guarded POST, recheck and escalate 409; preserve history |
| SD power loss/rename behavior | Same-directory temp, flushed backups and journal; hardware fault testing |
| Wi-Fi/TLS/clock/SDK differences | Explicit errors, bounded worker, pinned build; real-console test matrix |
| Older port libraries/security updates | Inventory exact dependencies; review current upstream advisories before public release |
| Oversized catalog metadata | Page size/body/array caps; report limit rather than unbounded allocation |
| Save state core/version mismatch | Adapter disables states until a verified tuple or explicit acknowledgement exists |
| Shared save-folder filename collision | Stable ID-based installed names and persisted mapping |
| Unknown BIOS/archive/multi-file needs | Explicit capability notice; do not pretend every browsable platform is playable |

The incremental checklist and release gates are in `TASKS.md`. Hardware acceptance is in `HARDWARE_TEST_PLAN.md`.
