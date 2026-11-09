#!/bin/sh
# Assemble TFCC release folders from CI artifacts.
#
#   scripts/package_tfcc.sh <client.so> <client.dll> <tfcc_mm_i386.so> [out]
#
# Produces:
#   out/tfcc/                mod folder (Half-Life/tfcc): liblist.gam with
#                            fallback_dir "tfc", cl_dlls/client.{so,dll}.
#                            tfc.dll/tfc.so are NOT shipped (Valve binaries):
#                            install.txt tells the user to copy them from tfc/.
#   out/server/addons/tfcc_mm/  the server companion plugin + plugins.ini line
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SO=$1; DLL=$2; MM=$3; OUT=${4:-$ROOT/dist}
rm -rf "$OUT"; mkdir -p "$OUT/tfcc/cl_dlls" "$OUT/tfcc/dlls" "$OUT/server/addons/tfcc_mm"
cp "$ROOT/tfcc_dist/liblist.gam" "$OUT/tfcc/"
cp "$SO" "$OUT/tfcc/cl_dlls/client.so"
cp "$DLL" "$OUT/tfcc/cl_dlls/client.dll"
cat > "$OUT/tfcc/install.txt" <<'TXT'
TFCC install (client)
1. Copy this "tfcc" folder next to "tfc" in your Half-Life folder.
2. Copy tfc\dlls\tfc.dll (and tfc.so on Linux) into tfcc\dlls\ (needed only to host a listen server).
3. Restart Steam; "TFC Competitive Edition" appears in your library, or run: hl.exe -game tfcc
Everything else (maps, models, sounds, sprites, your config) is read from tfc/.
TXT
cp "$MM" "$OUT/server/addons/tfcc_mm/"
echo "linux addons/tfcc_mm/tfcc_mm_i386.so" > "$OUT/server/addons/tfcc_mm/plugins.ini.line"
echo "packaged into $OUT"
