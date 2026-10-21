#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
image='devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5'
exec docker run --rm -v "$PWD:/work" -w /work "$image" make "$@"
