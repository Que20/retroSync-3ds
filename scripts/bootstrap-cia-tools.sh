#!/bin/sh
# Build pinned open-source host tools without installing anything globally.
set -eu
cd "$(dirname "$0")/.."
tools="$(pwd)/build-host/cia-tools/$(uname -s)-$(uname -m)"
mkdir -p "$tools/src" "$tools/bin"
checkout() {
    url=$1
    revision=$2
    destination=$3
    if [ ! -d "$destination/.git" ]; then
        git init "$destination"
        git -C "$destination" remote add origin "$url"
    fi
    git -C "$destination" fetch --depth 1 origin "$revision"
    git -C "$destination" checkout --detach FETCH_HEAD
}
checkout https://github.com/3DSGuy/Project_CTR d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8 "$tools/src/Project_CTR"
checkout https://github.com/carstene1ns/3ds-bannertool 734d33be79fd3f8c29c6296158f06ac7c5ca9dcb "$tools/src/bannertool"
# The pinned makerom reuses VER_MINOR for both its own version macro (19) and
# the title-version array index (1). Give title indices distinct names.
python3 - "$tools/src/Project_CTR/makerom/src" <<'PY'
from pathlib import Path
import re, sys
root = Path(sys.argv[1])
for name in ('user_settings.h', 'user_settings.c', 'cia.c'):
    p = root / name
    lines = p.read_text().splitlines(keepends=True)
    p.write_text(''.join(line if 'APP_NAME, VER_MAJOR' in line else
                        re.sub(r'\bVER_(MAJOR|MINOR|MICRO)\b', r'TITLE_VER_\1', line)
                        for line in lines))
PY
make -C "$tools/src/Project_CTR/makerom" deps -j4
make -C "$tools/src/Project_CTR/makerom" -j4
cp "$tools/src/Project_CTR/makerom/bin/makerom" "$tools/bin/"
# Upstream initializes a variable-length array, rejected by Clang. Its length is
# always <= 4; use a fixed four-byte buffer, keeping the original write length.
python3 - "$tools/src/bannertool/source/3ds/lz11.cpp" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
old, new = 'u8 pad[padLength] = {0};', 'u8 pad[4] = {0};'
assert old in s or new in s, 'Pinned bannertool source differs'
p.write_text(s.replace(old, new))
PY
banner="$tools/src/bannertool/source"
"${CXX:-c++}" -std=c++17 -O2 '-DVERSION="1.2.3"' -I"$banner" -I"$banner/pc" \
    "$banner"/*.cpp "$banner"/3ds/*.cpp "$banner"/pc/*.cpp -o "$tools/bin/bannertool"
echo "CIA tools ready: $tools/bin"
