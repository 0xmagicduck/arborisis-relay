#!/usr/bin/env bash
# Vendor MeshCore (https://github.com/meshcore-dev/MeshCore, MIT) into mesh/.
#
#   tools/sync_meshcore.sh <commit-or-tag> [path-to-existing-checkout]
#
# Arborisis Mesh builds on MeshCore's hardware layer (variants/, boards/,
# src/helpers/radiolib) and runs its repeater, so the tree below mirrors
# MeshCore's layout: the variant ini files use paths relative to that
# layout (-I variants/<board>, +<../variants/<board>>) and must find them
# where MeshCore puts them. Nothing under the directories listed in
# VENDORED is edited by hand: our code lives in app/, test/, tools/, docs/
# and platformio.ini / arborisis_envs.ini. Re-run this script to move to a
# newer MeshCore, then regenerate the environments (tools/gen_envs.py) and
# rebuild.
set -euo pipefail

REF="${1:?usage: sync_meshcore.sh <commit-or-tag> [checkout]}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${2:-}"

if [ -z "$SRC" ]; then
  SRC="$(mktemp -d)/MeshCore"
  git clone --quiet https://github.com/meshcore-dev/MeshCore.git "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$REF" 2>/dev/null || true
git -C "$SRC" checkout --quiet "$REF"
COMMIT="$(git -C "$SRC" rev-parse HEAD)"

VENDORED="src lib variants boards arch examples bin"
for d in $VENDORED; do
  rm -rf "${HERE:?}/$d"
  cp -a "$SRC/$d" "$HERE/$d"
done
for f in create-uf2.py merge-bin.py; do
  cp -a "$SRC/$f" "$HERE/$f"
done
cp -a "$SRC/license.txt" "$HERE/LICENSE-MeshCore.txt"

echo "$COMMIT" > "$HERE/MESHCORE_VERSION"
echo "MeshCore $COMMIT vendored into $HERE"

# The MeshCore repeater, compiled against the Arborisis radio arbiter: the
# three files of examples/simple_repeater that make the repeater (its
# main.cpp and UITask are replaced by app/main.cpp and app/ArbUI), with one
# line added — app/mc/McRedirect.h, which points `radio_driver` at the
# arbiter's MeshCore port.
mkdir -p "$HERE/app/mc"
for f in MyMesh.h MyMesh.cpp RateLimiter.h; do
  cp -a "$SRC/examples/simple_repeater/$f" "$HERE/app/mc/$f"
done
python3 - "$HERE/app/mc/MyMesh.cpp" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
anchor = '#include "MyMesh.h"\n'
assert anchor in s, "MyMesh.cpp no longer includes MyMesh.h first: update sync_meshcore.sh"
s = s.replace(anchor, anchor + '#include "McRedirect.h"   // Arborisis: radio_driver -> the arbiter\'s MeshCore port\n', 1)
open(p, "w").write(s)
PY
echo "app/mc refreshed from examples/simple_repeater"
