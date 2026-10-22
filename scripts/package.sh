#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test -s RetroSync3DS.3dsx || { echo 'Build RetroSync3DS.3dsx first.' >&2; exit 1; }
stage=dist/RetroSync3DS-0.1.0
mkdir -p "$stage/3ds/RetroSync3DS" "$stage/docs" "$stage/third_party/licenses" "$stage/third_party/cjson"
cp RetroSync3DS.3dsx "$stage/3ds/RetroSync3DS/RetroSync3DS.3dsx"
cp RetroSync3DS.smdh "$stage/3ds/RetroSync3DS/RetroSync3DS.smdh"
cp README.md LICENSE THIRD_PARTY.md "$stage/"
cp docs/*.md "$stage/docs/"
if [ -d docs/assets ]; then
    mkdir -p "$stage/docs/assets"
    cp docs/assets/*.png "$stage/docs/assets/"
fi
if [ -d docs/screenshots ]; then
    mkdir -p "$stage/docs/screenshots"
    cp docs/screenshots/*.png "$stage/docs/screenshots/"
fi
cp third_party/licenses/* "$stage/third_party/licenses/"
cp third_party/cjson/LICENSE "$stage/third_party/cjson/"
# Example only, never copy the user's live settings or credentials.
cp config/settings.example.ini "$stage/3ds/RetroSync3DS/settings.example.ini"
if [ -s dist/RetroSync3DS.cia ]; then
    python3 scripts/verify-cia.py dist/RetroSync3DS.cia
    mkdir -p "$stage/cia"
    cp dist/RetroSync3DS.cia "$stage/cia/RetroSync3DS.cia"
fi
python3 - <<'PY'
from pathlib import Path
import hashlib, zipfile
stage = Path('dist/RetroSync3DS-0.1.0')
files = sorted(p for p in stage.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
(stage / 'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(stage).as_posix()}\n' for p in files))
with zipfile.ZipFile('dist/RetroSync3DS-0.1.0.zip', 'w', compression=zipfile.ZIP_DEFLATED) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():
            z.write(p, p.relative_to(stage))
print('dist/RetroSync3DS-0.1.0.zip')
PY
