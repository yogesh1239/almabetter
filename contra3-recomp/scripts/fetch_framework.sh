#!/bin/sh
# Fetch the snesrecomp framework at the pinned revision and apply this port's
# patch (game-selected render scale in the desktop host; thread-local PPU
# render-path statics so presenter passes can rasterize on worker threads).
set -eu
cd "$(dirname "$0")/.."
pin=$(sed -n 's/^snesrecomp=//p' framework_pins.txt)
if [ ! -d snesrecomp/.git ]; then
  git clone https://github.com/RetroPortingToolKit/snesrecomp.git snesrecomp
fi
git -C snesrecomp fetch origin "$pin" 2>/dev/null || git -C snesrecomp fetch origin
git -C snesrecomp checkout --quiet "$pin"
git -C snesrecomp submodule update --init --recursive
if git -C snesrecomp apply --check ../patches/snesrecomp-contra3.patch 2>/dev/null; then
  git -C snesrecomp apply ../patches/snesrecomp-contra3.patch
  echo "framework at $pin, patch applied"
else
  echo "framework at $pin, patch already applied (or does not apply cleanly)"
fi
