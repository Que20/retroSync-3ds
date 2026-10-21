# RomM API compatibility — research snapshot, 2026-10-21

Status: source-verified contracts; **no live RomM instance has been authenticated or tested**.
The 0.1.0 binary only calls heartbeat, platform list, ROM list and save metadata list.
All write/download/session contracts below are research for the next milestone, not implemented capabilities.
Schemas show the fields relevant to this client, not every upstream field.

## Reproducible upstream references

| Upstream | Inspected revision | References |
| --- | --- | --- |
| RomM stable 5.4.0 | `68f71259f7c6de39fbeedff9cc9ac114c18ee250` | [tag source](https://github.com/rommapp/romm/tree/68f71259f7c6de39fbeedff9cc9ac114c18ee250) |
| RomM stable 4.9.0 | `2920ca96cfc5fca73b524b485055f51ebaa44911` | [tag source](https://github.com/rommapp/romm/tree/2920ca96cfc5fca73b524b485055f51ebaa44911) |
| RomM master | `e997ed7a0a2bece196aadbfabdc9a7961d124c8b` | [source](https://github.com/rommapp/romm/tree/e997ed7a0a2bece196aadbfabdc9a7961d124c8b); endpoint files inspected below match stable 5.4.0 |
| Grout | `15b02e269980d05768807686ad9ecbbd0879229c` | [source](https://github.com/rommapp/grout/tree/15b02e269980d05768807686ad9ecbbd0879229c) |
| open_agb_firm | `401c3dff900f658d3edf77ababcfaf5dca54a0a8` | [source](https://github.com/profi200/open_agb_firm/tree/401c3dff900f658d3edf77ababcfaf5dca54a0a8) |
| libctru | `9b55eda44cf80b971503991e0c78f9bc8fe50425` | [source](https://github.com/devkitPro/libctru/tree/9b55eda44cf80b971503991e0c78f9bc8fe50425) |
| 3DS examples | `be2001fee08cf8ec7d3366e095e83f6f75da8942` | [application template](https://github.com/devkitPro/3ds-examples/blob/be2001fee08cf8ec7d3366e095e83f6f75da8942/templates/application/Makefile) |

Online documentation also inspected: [current device sync](https://docs.romm.app/latest/developers/device-sync-protocol/),
[4.9.0 protocol](https://docs.romm.app/4.9.0/developers/device-sync-protocol/),
[client tokens](https://docs.romm.app/latest/developers/client-api-tokens/),
[API authentication](https://docs.romm.app/latest/developers/api-authentication/),
[RetroArch cloud sync](https://docs.romm.app/latest/ecosystem/retroarch-cloud-sync/).
`latest` is mutable; use the pinned backend source to resolve differences. Version-specific claims below were checked against both tags.

## Authentication and transport

Use `Authorization: Bearer rmm_...` client API tokens. Prototype scopes:
`platforms.read`, `roms.read`, optionally `assets.read` for save history.
Future sync additionally needs `assets.write`, `devices.read`, `devices.write`.
Account visibility/hidden platform/ROM policy can further restrict access. A scope is not a bypass.
Prototype does not request passwords, use browser cookies, or perform pairing.
Session-cookie mutation requests require the CSRF cookie/header; bearer requests bypass this check.

Scope strings are defined in [auth/constants.py](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/handler/auth/constants.py).
Create/revoke a scoped client token in the server's web UI. The current token-management API has
`POST /api/client-tokens` (JSON name/scopes/expiry, `me.write`), but this client deliberately does not implement account management;
see [client_tokens.py](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/client_tokens.py).

HTTPS uses libcurl/mbedTLS with **both peer and hostname verification enabled**. Bundled Mozilla CA roots
are embedded in RomFS; an explicit SD CA bundle can replace them for a private CA. Certificate/hostname/time errors
are shown; there is no insecure TLS switch. Redirects are rejected to prevent bearer-token forwarding.
HTTP requires explicit LAN opt-in and the UI warns that it exposes the token.

## Endpoint matrix

Paths are relative to the configured server base URL (which may include a reverse-proxy prefix).
No filename from a response is treated as a local path.

| Method and URL | Request | Relevant response | Scope | 0.1.0 |
| --- | --- | --- | --- | --- |
| `GET /api/heartbeat` | None | `{SYSTEM:{VERSION:string,...},...}` | Public | Implemented; version is informational |
| `GET /api/platforms` | Optional `updated_after` | Array `{id,slug,fs_slug,name,custom_name,display_name,rom_count,...}` | `platforms.read` | Implemented; sorted by display name; array is **not paginated** upstream |
| `GET /api/roms` | `platform_ids=<id>&limit=20&offset=0&search_term=<encoded>&order_by=name&order_dir=asc`; disable `with_char_index`, `with_filter_values`, `with_files`, `with_rom_id_index` | `{items:[{id,platform_id,fs_name,fs_size_bytes,name,summary,...}],total,limit,offset,...}` | `roms.read` | Implemented; only one page retained |
| `GET /api/roms/{id}` | Positive integer ID | Detailed ROM including `files:[{id,file_name,file_size_bytes,md5_hash,sha1_hash,crc_hash,...}]` | `roms.read` | Future download identity/file validation |
| `HEAD` / `GET /api/roms/{id}/content/{file_name}` | URL-encoded name; optional comma-separated `file_ids` | Bytes or download/conversion response, not JSON; may archive multiple files | `roms.read` by default; server can disable download auth | Not implemented; never treat the generic endpoint as guaranteed `.gba` bytes |
| `GET /api/saves?rom_id={id}` | Optional `slot`, `device_id`; newer versions also allow ROM scopes | Array SaveSchema (below), including historical/manual rows | `assets.read`; `devices.read` if device supplied | Implemented metadata only; no assumption about active slot |
| `GET /api/saves/{id}` | Optional `device_id` | SaveSchema | `assets.read` (+ device scope) | Research only |
| `GET /api/saves/{id}/content` | Optional `device_id`, `session_id`, `optimistic=false` | Raw save bytes | `assets.read`, plus `devices.read` with device | Research only; explicit confirmation required after local commit |
| `POST /api/saves` | Query `rom_id,emulator,slot,device_id,session_id,overwrite=false,autocleanup=false`; multipart **`saveFile`**, optional `screenshotFile`; 5.4 also `content_hash` | Stored SaveSchema; `409` for guarded concurrent changes | `assets.write`, plus `devices.write` with device | Research only; retain history, avoid automatic cleanup |
| `PUT /api/saves/{id}` | Multipart `saveFile`, optional screenshot and device metadata; 5.4 `content_hash` | SaveSchema | `assets.write` (+ device scope) | Research only; lacks slot conflict guard; do not use for routine overwrite |
| `POST /api/saves/{id}/downloaded` | JSON `{device_id}`; 5.4 optionally `content_hash` | SaveSchema with device sync records | `devices.write` | Research only; see version caveats |
| `GET /api/states?rom_id={id}` / `GET /api/states/{id}/content` | ID/filter | Array StateSchema / bytes | `assets.read` | Disabled for open_agb_firm |
| `POST /api/states` | Query `rom_id`, optional `emulator`; multipart **`stateFile`**, optional screenshot | StateSchema | `assets.write` | Future verified core/version metadata required |
| `POST /api/devices` | DeviceCreatePayload below | `{device_id:string,name,created_at}` | `devices.write` | Research only |
| `GET /api/devices` / `GET /api/devices/{id}` | Caller-owned devices | Device array / DeviceSchema (`id`, not registration's `device_id`) | `devices.read` | Research only |
| `PUT /api/devices/{id}` | Device properties, optional `sync_enabled` | DeviceSchema | `devices.write` | Research only |
| `POST /api/sync/negotiate` | Version-dependent JSON below | Session ID and operations | `assets.read` + `devices.read` | Research only; creates server state |
| `POST /api/sync/sessions/{id}/complete` | `{operations_completed:int,operations_failed:int,play_sessions?:[...]}` | `{session:SyncSessionSchema,play_session_ingest:...}` | `devices.write` | Research only; require confirmed result |
| `GET /api/sync/sessions` / `GET /api/sync/sessions/{id}` | Device filter; pagination varies by release | Session list / SyncSessionSchema | `devices.read` | Research only |

Sources: [heartbeat](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/heartbeat.py),
[platform routes](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/platform.py),
[ROM routes](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/roms/__init__.py),
[ROM schemas](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/responses/rom.py),
[ROM filters](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/handler/database/rom_filters.py),
[page parameters](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/responses/base.py),
[save routes](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/saves.py),
[state routes](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/states.py),
[asset schemas](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/responses/assets.py),
[devices](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/device/__init__.py).

SaveSchema includes `id,rom_id,user_id,file_name,file_size_bytes,emulator,slot,content_hash,updated_at,created_at,
origin_device_id,device_syncs`. `content_hash` can be null. A null slot is archival/manual, not automatically the
active save. Empty string and null are distinct; do not trim or merge slot labels. Multiple rows can belong to one slot.
`StateSchema` includes emulator and (in 5.4) `core`, but the response does not establish core **build/version** compatibility.
Core metadata alone is insufficient to authorize a state restore.

## Verified device protocol and release differences

**Yes, negotiate exists in the 4.9.0 tag. Do not infer that every older release supports it.**
No deployment version was supplied; the prototype does not negotiate or register a device.

Device registration JSON supports `name,platform,client,client_version,hostname,mac_address,sync_mode,sync_config`.
5.4 adds `capabilities` and duplicate/existing controls. `sync_mode:"api"` selects this transport.
Registration returns `device_id`, a string UUID, not a numeric ID. Persist the returned ID scoped to server/account.
5.4 matches existing devices by MAC or hostname/platform; do not invent a MAC address or silently duplicate devices.

4.9.0 negotiate request:

```json
{"device_id":"<registered UUID>","saves":[{"rom_id":123,"file_name":"game.srm","slot":"autosave","emulator":null,"content_hash":"<MD5 hex>","updated_at":"2026-10-21T10:00:00Z","file_size_bytes":32768}]}
```

Response: `{session_id,operations:[{action,rom_id,save_id,file_name,slot,emulator,reason,
server_updated_at,server_content_hash}],total_upload,total_download,total_conflict,total_no_op}`.
Actions in 4.9.0 are `upload`, `download`, `conflict`, `no_op`. Pairing uses `(rom_id,slot)`;
null-slot client entries are archival uploads, not a reliable sync key.

| Behavior | 4.9.0 tag | 5.4.0 tag |
| --- | --- | --- |
| Required device | `device_id` required | May be inferred from a device-bound token |
| Scope downloads to installed ROM IDs | No `rom_ids` field; can return remote saves outside local set | `rom_ids` supported; omission is read-only, not deletion |
| Restore missing local saves | No `restore_unlisted` request field | `restore_unlisted` available |
| Filter producer | No `emulators` field | `emulators` list supported |
| Concurrent negotiations | Cancels existing active sessions for device | Allows sessions per launch |
| Delete operation | Not in response schema | `delete` plus `total_delete` supported |
| Content baselines | Timestamp-based device bookkeeping; no client hash parameter on upload/ack | Client/server baseline hashes tracked; upload/ack accept content hash |
| Download acknowledgement | `{device_id}` only | `{device_id,content_hash?}` |

Sources: [4.9 sync](https://github.com/rommapp/romm/blob/2920ca96cfc5fca73b524b485055f51ebaa44911/backend/endpoints/sync.py),
[4.9 sync schema](https://github.com/rommapp/romm/blob/2920ca96cfc5fca73b524b485055f51ebaa44911/backend/endpoints/responses/sync.py),
[4.9 saves](https://github.com/rommapp/romm/blob/2920ca96cfc5fca73b524b485055f51ebaa44911/backend/endpoints/saves.py),
[5.4 sync](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/sync/__init__.py),
[5.4 response](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/responses/sync.py),
[5.4 comparison](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/handler/sync/comparison.py).

Important: upstream comparison can use timestamps without a baseline. Never trust an initial "newer" proposal
as permission to overwrite differing bytes. RetroSync's tested local planner makes that a conflict.
After EEPROM conversion, local bytes differ from server bytes by design: a future manifest must store both
raw hashes and a common canonical hash, not compare the two representations directly.

### Transfer and completion semantics to implement

1. Negotiate once per user action. Filter operations to installed ROM IDs even on 4.9.0. Reject unknown actions.
2. Recheck remote/local hashes against the persisted baseline; treat ambiguous or changed preconditions as conflict.
3. Download via `/api/saves/{id}/content?...&optimistic=false`. Validate bytes, convert only verified formats,
   back up losing copy, commit locally, then acknowledge. Do not record successful sync on receipt of HTTP headers.
4. Upload using `POST /api/saves?...&overwrite=false&autocleanup=false`. Handle `409` by refetching and asking again.
   Explicit conflict overwrite must retain local backup **and** the previous remote version. `PUT` is not concurrency-safe.
5. Do not blindly retry a POST after a lost response: inspect slot history/hash and journal first.
   There is no verified universal idempotency-key contract. Timestamped slot history must not be confused with a new active slot.
6. Read/confirm the server outcome and local manifest commit, then POST completed/failed counts. The session response,
   not just the request being sent, gates "complete". A cancelled/failed item remains unresolved. Never execute server `delete` automatically.

## Capability detection and fallback policy

Implemented now: heartbeat version display, strict validation of each used response shape, clear `401`, `403`,
`404`/`405`, redirects and unexpected schema errors. A public heartbeat succeeding does not prove authentication;
the connection test also makes the authenticated platform request. Unknown extra JSON fields are ignored.
Missing required fields, excessive arrays and oversized bodies fail closed. There is no automatic version allowlist
that enables writes.

Before enabling device sync: obtain the deployment's OpenAPI through its documented API reference or exported schema;
check method/path **and** supported fields, then select a tested release profile. A proxy may hide the schema;
missing discovery is "unknown", not "unsupported". Do not probe POST negotiate with an empty list: that creates a
session and can return remote downloads. Do not confuse a permission failure with missing support.

If device sync is unavailable, use per-ROM save listing/content and the local three-way baseline. Require an explicit
slot choice, compute remote hashes from downloaded bytes when absent, and detect races before every write.
If the server cannot guard a write, retain browsing and manual import/export only: do not advertise safe automatic
two-way sync. **0.1.0's fallback is read-only browsing; this transfer fallback is a design, not an implementation.**

## Grout inspiration and RetroArch transport choice

Inspected [UI browsing](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/ui/games_list.go),
[ROM API mappings](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/romm/roms.go),
[firmware save folders](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/cfw/saves.go),
[sync resolve/discovery](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/saves/flow.go),
[conflict escalation](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/saves/conflicts.go),
[sync UI](https://github.com/rommapp/grout/blob/15b02e269980d05768807686ad9ecbbd0879229c/ui/save_sync.go).
Useful ideas: platform-first browse/search, adapters for path conventions, stable ID mappings, per-game slot preferences,
negotiate/execute split, returning to conflict UI after a concurrent `409`, remote-only discovery after local storage loss.
No Go/Linux UI, full-catalog cache, blanket TLS bypass, or nil-slot coercion is ported into the 3DS application.

RomM 5.4 implements WebDAV under `/api/sync/retroarch/` with OPTIONS/PROPFIND/GET/HEAD/PUT/DELETE/MKCOL/LOCK/UNLOCK;
it issues a Basic authentication challenge and enforces asset read/write scopes. See
[retroarch.py](https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/sync/retroarch.py).
Choose the RomM asset/device API for RetroSync: stable ROM IDs, explicit slots/history and negotiation fit this client.
Do not depend on native cloud-sync support in a particular 3DS RetroArch build; that remains unverified.
WebDAV support alone does not make save states cross-core compatible.
