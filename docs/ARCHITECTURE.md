# V1 architecture

The app uses C++17/libctru, citro2d/citro3d, libcurl/mbedTLS, zlib and a bounded cJSON parser.
All networking, hashing, extraction and disk transactions run in one cancellable worker. Only the UI thread calls
GPU/input/applets. Jobs own settings and output snapshots; completion uses release/acquire atomics and a joined thread.

| Module | Responsibility |
| --- | --- |
| `source/main.cpp` | Screens, input/touch, worker ownership, software-keyboard login and conflict choices |
| `source/ui.cpp` | Native dual-screen cards, typography, navigation and transfer progress |
| `source/core/http.cpp` | Same-origin validated HTTPS, OAuth login/refresh, bounded JSON, streamed downloads and multipart uploads |
| `source/core/api.cpp` | Strict catalog/save decoding and ROM query generation |
| `source/core/library.cpp` | Verified account identity, installs, OAF mappings, slot binding and guarded two-way sync |
| `source/core/storage.cpp` | Streaming digests, SD transactions/recovery, bounded ZIP extraction and GBA validation |
| `source/core/core.cpp` | Settings persistence, URL/path checks, OAF INI parsing, format conversion and three-way decisions |

## Memory and cancellation

SOC uses a 1 MiB aligned buffer. The worker stack is 128 KiB. JSON bodies are limited to 512 KiB and nesting to 64.
Catalog pages hold at most 20 games and adapt to five/one when the response is oversized; page navigation uses the
actual returned count and prior offsets. ROM bytes are streamed to SD; ZIP deflate uses fixed buffers. Save batteries
are limited to 128 KiB. The UI stays responsive while the worker is active; B sets cancellation.

Connect timeout is eight seconds; metadata requests have a 25-second ceiling; file transfers have a ten-minute ceiling
and a low-throughput timeout. SDK resolver/storage calls can delay cancellation; hardware latency is not assumed.
New 3DS speed-up is enabled. SD free-space display is cached instead of polled every frame.

## Account and transaction identity

`/api/users/me` supplies the account ID. SHA256 of the configured base URL and account ID scopes the manifest/device
folder. Installed ROM names include that namespace and stable RomM ID, preventing duplicate-title collisions.
Each game record stores the ROM path/hash, exact remote slot/producer, format/type, OAF path, local/server baseline
hashes, and pending upload evidence. Metadata writes retain prior backups; file replacements use journals and verified
content-addressed backups because libctru rename cannot be treated as POSIX atomic replacement on FAT.

A staged install can be recovered after restart. Changed/untracked files are preserved. Missing tracked saves do not
propagate deletion. Downloaded server save originals and local replacement backups are retained for review.

## Save protocol

RomM 5.4.0 is the enabled write profile. Per-game asset/device APIs are used; negotiate/session completion is not.
The current named slot is resolved from save rows using the server guard's timestamp/ID ordering. A canonical
three-way hash comparison chooses unchanged, upload, download or conflict. Both changed defaults to Cancel.

Before mutation, both sides are rechecked. Downloads use `optimistic=false` and are acknowledged after local commit.
Uploads use a registered device, exact-version acknowledgement and `overwrite=false&autocleanup=false`. HTTP 409 is
reported for review. A lost upload response persists `upload-unconfirmed`; history evidence is required before another
write. Session rotation is persisted immediately, with no password storage.

See [V1 behavior and recovery](V1.md), [API contracts](API_COMPATIBILITY.md), and
[hardware acceptance](HARDWARE_TEST_PLAN.md) for compatibility limits and remaining validation.
