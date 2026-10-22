#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test -s RetroSync3DS.elf || { echo 'Build the ARM ELF first.' >&2; exit 1; }
tools="$(pwd)/build-host/cia-tools/$(uname -s)-$(uname -m)"
makerom=${MAKEROM:-$tools/bin/makerom}
bannertool=${BANNERTOOL:-$tools/bin/bannertool}
if [ ! -x "$makerom" ] || [ ! -x "$bannertool" ]; then
    echo 'Run: sh scripts/bootstrap-cia-tools.sh (in the same environment as this build).' >&2
    exit 1
fi
mkdir -p build/cia dist
"$bannertool" makesmdh -s 'RetroSync 3DS' -l 'Download games and sync saves' \
    -p 'RetroSync contributors' -i assets/icon.png -r regionfree \
    -f visible,recordusage,nosavebackups,new3ds -o build/cia/icon.smdh
"$bannertool" makebanner -i assets/banner.png -a assets/silence.wav -o build/cia/banner.bnr
# Rebuild a native executable NCCH from ELF; this is not a 3DSX forwarder.
"$makerom" -f cia -o dist/RetroSync3DS.cia -elf RetroSync3DS.elf \
    -rsf assets/retrosync.rsf -icon build/cia/icon.smdh -banner build/cia/banner.bnr \
    -target t -exefslogo -major 1 -minor 0 -micro 0
python3 scripts/verify-cia.py dist/RetroSync3DS.cia
