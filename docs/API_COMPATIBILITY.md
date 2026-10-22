# RomM API compatibility — V1

Implemented write profile: **RomM 5.4.0**. Live username/password login, catalog, ROM download and isolated save
upload/download/conflict tests pass against that release. Other versions may browse, but save writes fail closed until
the profile is verified. A successful heartbeat alone is not authentication.

## Used contracts

| Method / path | Request / result | Scope |
| --- | --- | --- |
| POST `/api/token` | OAuth form: password grant + username/password/scope; or refresh grant. Reads access_token, refresh_token, token_type | Requested scopes below |
| GET `/api/heartbeat` | `SYSTEM.VERSION` | Public |
| GET `/api/users/me` | Verified numeric account ID | `me.read` |
| GET `/api/platforms` | Platform IDs, names, slugs | `platforms.read` |
| GET `/api/roms` | platform_ids, limit, offset, order_by=name, order_dir=asc, encoded search_term; disable with_char_index/filter_values/rom_id_index/files | `roms.read` |
| GET `/api/roms/{id}` | Explicit single top-level file ID/name/size/MD5 | `roms.read` |
| GET `/api/roms/{id}/content/{encoded_name}?file_ids={id}` | Stream exact file to SD | `roms.read` |
| GET `/api/saves?rom_id={id}&slot={encoded_slot}` | Slot history; exact null/string distinction, owner/ROM IDs and hashes | `assets.read` |
| GET `/api/saves/{id}` | Recheck the exact version before acknowledgement/confirmation | `assets.read` |
| GET `/api/saves/{id}/content?optimistic=false` | Battery bytes without optimistic sync bookkeeping | `assets.read` |
| POST `/api/devices` | Stable local hostname, client/platform/version, sync_mode=api, allow_existing=true; returns device_id UUID | `devices.write` |
| GET `/api/devices/{id}` | Verify owner and sync_enabled | `devices.read` |
| POST `/api/saves/{id}/downloaded` | JSON device_id and verified content_hash | `devices.write` |
| POST `/api/saves` | Multipart saveFile; rom_id, exact slot/producer, device_id, content_hash, overwrite=false, autocleanup=false | `assets.write`, device use requires `devices.write` |

Login requests `me.read platforms.read roms.read assets.read assets.write devices.read devices.write`.
The password is not stored. OAuth refresh-token rotation is persisted immediately; unknown write outcomes are never
retried as though they were rejected. Library responses and credentials are not logged.

## Active slot and races

A named slot is exact: null is archival and an empty string is not silently renamed to autosave. The current row is
selected with the same `(updated_at DESC, id DESC)` order used by RomM's guarded write. The client parses UTC/offset
ISO timestamps and uses the ID tie-breaker. The summary endpoint is deliberately avoided: live tests showed it could
return an older row when uploads share a timestamp. A changed producer requires format review.

Remote bytes are verified against the server MD5 and preserved before acknowledgement. Acknowledgement rechecks
row hash/time. The device guard rejects a newer unacknowledged version at upload time. Forced overwrite, remote PUT,
autocleanup and automatic deletion are not used. A completed upload is read back before advancing the local baseline;
a lost response is reconciled against history using the intended hash. No universal idempotency contract is assumed.

Server timestamps order versions inside a slot; they do not decide local-versus-remote conflict winners. That decision
uses canonical content and the previous successful baseline. Save conversion requires an explicitly verified chip type
and producer representation. State files and unknown containers remain unsupported.

## Source references

Contracts are checked against the [RomM 5.4.0 source](https://github.com/rommapp/romm/tree/5.4.0/backend):
[authentication](https://github.com/rommapp/romm/blob/5.4.0/backend/endpoints/auth.py),
[ROM routes](https://github.com/rommapp/romm/blob/5.4.0/backend/endpoints/roms/__init__.py),
[save routes](https://github.com/rommapp/romm/blob/5.4.0/backend/endpoints/saves.py),
[save ordering](https://github.com/rommapp/romm/blob/5.4.0/backend/handler/database/saves_handler.py),
[device routes](https://github.com/rommapp/romm/blob/5.4.0/backend/endpoints/device/__init__.py).

OAF mapping/type names and EEPROM behavior follow the inspected
[open_agb_firm source](https://github.com/profi200/open_agb_firm/tree/401c3dff900f658d3edf77ababcfaf5dca54a0a8)
and [EEPROM conversion reference](https://github.com/exelotl/gba-eeprom-save-fix/tree/fdc61d84c6b7779dd39786a70ee98739c9568001).
Do not infer an arbitrary emulator's battery representation from a `.sav` extension or length alone.
