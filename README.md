# RetroSync 3DS

A New Nintendo 3DS homebrew client for a self-hosted RomM library, prioritizing open_agb_firm GBA support.

**0.1.0 is the first compiling prototype: library browsing and read-only save information. ROM downloads and
two-way save synchronization are not implemented yet.** The `.3dsx` has been cross-compiled; it has not been booted
on a real console or connected to an authenticated live RomM server. No copyrighted ROMs, BIOS or save files are included.

## Available now

- Dual-screen controller/touch UI: platform list, paginated games, metadata/status and save history.
- URL/token settings, connection test, search, SD free-space display and local disconnect/reset.
- Wi-Fi requests on a cancellable worker with bounded JSON memory and timeouts.
- HTTPS with CA and hostname validation, bundled CA roots and optional private CA bundle.
- open_agb_firm global config status; portable, tested helpers for paths/slots, explicit save formats and hash-based reconciliation.
- Reproducible devkitPro build, desktop mock HTTP/HTTPS tests, source compatibility notes and hardware acceptance plan.

## Install the prototype

Build/package instructions below generate `dist/RetroSync3DS-0.1.0.zip`. Extract its `3ds` directory to the SD root:

```text
/3ds/RetroSync3DS/RetroSync3DS.3dsx
/3ds/RetroSync3DS/RetroSync3DS.smdh
```

Launch with Homebrew Launcher on your New 3DS with modern Luma3DS/boot9strap. No auto-launch or emulator integration
is needed. Alternatively install the native CIA with FBI as described below. Both launch methods still require hardware validation.

### Installation via FBI avec QR code

Le fichier `dist/RetroSync3DS.cia` peut être installé directement sur le menu HOME d'une New 3DS sous Luma3DS.

Dans FBI, choisis **Remote Install → Scan QR Code**, puis scanne ce QR :

![Installer RetroSync 3DS avec FBI](docs/assets/fbi-install.png)

[Télécharger le CIA de la dernière release](https://github.com/Que20/retroSync-3ds/releases/latest/download/RetroSync3DS.cia)
· [Toutes les releases](https://github.com/Que20/retroSync-3ds/releases)

La 3DS doit être connectée à Internet. Aucun serveur sur ton ordinateur n'est nécessaire : le QR pointe directement
vers le fichier CIA publié sur GitHub. Il reste valable pour les prochaines releases.

Le CIA est compilé et ses structures/hachages sont contrôlés, mais l'installation et le démarrage sur console restent
à valider. Il contient le prototype de navigation ; les téléchargements et la synchronisation des sauvegardes
restent indisponibles.

<details>
<summary>Alternative : distribuer un build local par QR sur le réseau local</summary>

Sur l'ordinateur, installe les dépendances du serveur QR dans un environnement Python séparé :

```sh
python3 -m venv build-host/packaging-venv
build-host/packaging-venv/bin/pip install -r scripts/requirements-packaging.txt
build-host/packaging-venv/bin/python scripts/serve-cia.py --host ADRESSE_IP_LOCALE_DU_PC
```

Ouvre ensuite `dist/RetroSync3DS-FBI-QR.png` et, dans FBI, choisis **Remote Install → Scan QR Code**.
La 3DS et l'ordinateur doivent être sur le même réseau local. Garde l'ordinateur allumé et le serveur actif pendant
l'installation. Le serveur publie uniquement le CIA, sur le port 8766 ; il ne publie ni les sources ni les paramètres.
Si le pare-feu le demande, autorise cette connexion sur le réseau local. Ctrl-C arrête le serveur.
Le QR contient une URL HTTP locale directe ; il n'est pas utilisable depuis Internet et doit être régénéré si l'IP change.

</details>

Sans réseau local, copie `dist/RetroSync3DS.cia` dans `/cias/` sur la SD puis utilise FBI → SD → cias → Install CIA.

Press SELECT to configure:

1. Set your final RomM URL, e.g. `https://romm.example.net` (no trailing slash; reverse-proxy prefix is supported).
2. In RomM's web UI, create a scoped client API token with `platforms.read`, `roms.read`, and `assets.read` for save info.
   Enter it using the masked token field. The prototype never asks for your account password.
3. Set the future ROM directory if desired (`sdmc:/roms/gba` by default). No ROM directory is created by this build.
4. Choose Save settings, then Test connection / load library. An authenticated platform request follows heartbeat.

Alternatively copy `config/settings.example.ini` to `/3ds/RetroSync3DS/settings.ini` and edit it on your computer.
Keep real tokens out of source control. Settings and recovery backups are **plaintext on SD**. Local disconnect erases
these files; revoke the token in RomM as well. Flash storage cannot promise secure erasure.

The public CA bundle is embedded in the application. For a private CA, place its PEM root bundle at
`/3ds/RetroSync3DS/cacert.pem` (this replaces the bundled roots). Fix certificate/hostname/console clock problems;
there is no TLS-verification bypass. HTTP is available only through explicit unsafe-LAN opt-in and exposes the token
to that network. Redirects are refused: configure the final URL rather than a redirecting address.

## Controls

| Control | Action |
| --- | --- |
| D-pad / touch list | Select item |
| A / bottom-right touch action | Open platform or edit selected setting; game download action reports unavailable |
| B / bottom-left touch action | Back; cancel in-flight request |
| L / R | Previous / next server page of games |
| Y / bottom-middle touch action | Connect from platforms; search within current platform |
| X on a game | Read-only remote save history and metadata |
| SELECT | Settings / return to platforms |
| START | Cancel pending request and exit |

The top screen shows selected game/save information, SD space and full wrapped status. The bottom screen shows
ten rows at a time from a 20-ROM API page. Save history is informational: no historical save is selected for restore.

## Build

The build was verified with official devkitPro `devkitARM r68`, `libctru 2.7.0`, `3ds-curl 8.4.0`,
`3ds-mbedtls 2.28.8` and `3ds-zlib 1.3.1`, using the pinned container digest in `scripts/build-container.sh`.

With Docker running:

```sh
sh scripts/build-container.sh -j4
sh scripts/package.sh
```

To also build the CIA, first run the pinned host-tool bootstrap inside that same container, then `make cia`:

```sh
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5 sh scripts/bootstrap-cia-tools.sh
sh scripts/build-container.sh -j4 cia
sh scripts/package.sh
```

The official image is large on first pull. Outputs: `RetroSync3DS.3dsx`, `.elf`, `.smdh`, optional `dist/RetroSync3DS.cia`, and the install ZIP in `dist/`.
The package includes documentation, third-party license notices and checksums, and never copies live settings.
On a native devkitPro install, install `3ds-dev`, `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`, set `DEVKITPRO`/`DEVKITARM`
as documented by devkitPro, then run `make -j4`. The Makefile derives from the official 3DS application template.
It embeds `romfs/cacert.pem`; no system trust store is assumed on the console.

## Desktop tests and mock server

Requires a C/C++17 compiler, Make, Python 3, OpenSSL CLI and libcurl development headers/library
(on Debian/Ubuntu: `libcurl4-openssl-dev pkg-config`).

```sh
make -f Makefile.host test
```

ASan/UBSan are enabled by default. Tests use synthetic metadata and generated battery bytes; loopback HTTP/HTTPS
tests verify authentication headers/errors, redirect refusal, size caps, cancellation and TLS certificate rejection.
These use **desktop libcurl**, not the ARM/mBedTLS binary or real Wi-Fi. See `docs/BUILD_VERIFICATION.md` for results.

For interactive browsing without a real RomM instance:

```sh
python3 scripts/mock_romm.py
```

For hardware LAN testing bind an explicitly selected desktop LAN IP using `--bind <LAN-IP>`; configure the app with
`http://<LAN-IP>:8765`, token `rmm_mock_token`, and enable HTTP. This read-only mock serves 45 synthetic GBA entries,
an empty Game Boy platform and sample save metadata. It serves no ROM/save bytes and does not emulate sync APIs.

## Compatibility and limits

The source investigation covers RomM **4.9.0 and 5.4.0**. Both have device negotiation, with significant schema and
behavior differences; this does not mean every deployment/version is supported. The prototype validates the response
shape of its four read APIs and shows authentication, missing-endpoint or schema errors. No write capability is
automatically enabled from a version string. Unknown deployments keep transfers unavailable.

ROM pages are 20 items; JSON is capped at 512 KiB; platforms and save history at 512 entries. Larger metadata responses
fail with an explanation. Some older RomM pages can include a full ROM ID index even with page limits; these may hit
the response cap. No cover cache or archive extraction exists. Browsing a platform does not establish playability or
BIOS availability. Installed/local save status is not tracked yet.

OAF save states are unsupported. `.srm` is not automatically renamed to `.sav`: EEPROM byte order and save chip type
require verified provenance. The tested converter has not been validated using real EmulatorJS/OAF gameplay. OAF
per-game INI handling is specified but not wired to operations. RetroArch adapters, core/version-safe states, bulk sync,
file backups/journal recovery, transfer retries, audit logs and the required conflict modal are future work.

English console text is intentionally basic; metadata is sanitized to printable ASCII. Full French/UTF-8 rendering and
complete string externalization remain open. Cancellation/SDK resolver timing, touch layout, sleep/resume, SD durability
and actual TLS connections require real-console tests. Existing port libraries need a security update review before
public production release.

## Engineering notes

- [API contracts, scopes, pinned source references and version differences](docs/API_COMPATIBILITY.md)
- [Architecture, recovery design and risks](docs/ARCHITECTURE.md)
- [Emulator path and save-format matrix](docs/EMULATOR_MATRIX.md)
- [Incremental task checklist and release gates](docs/TASKS.md)
- [Real-device and two-way save test plan](docs/HARDWARE_TEST_PLAN.md)
- [Build/test evidence](docs/BUILD_VERIFICATION.md)
- [CIA packaging, identity, tool references and verification](docs/CIA_PACKAGING.md)

Project code is MIT licensed. See `THIRD_PARTY.md` for vendored and linked dependency notices. Grout and RomM were
inspected for architecture/API inspiration; their Linux/Go code is not embedded in this application.
