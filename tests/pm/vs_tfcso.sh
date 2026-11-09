#!/bin/sh
# Prediction parity against the real server: run pm_diff with Valve's tfc.so
# (it exports PM_Init/PM_Move/PM_FlyMove/pmove) as the baseline and the TFCC
# pm_shared as the candidate. Different compilers round x87 math differently,
# so frames are compared with a tolerance (PM_TOL, default 0.01 units).
#
#   TFC_SO=/path/to/tfc/dlls/tfc.so tests/pm/vs_tfcso.sh [scenarios] [frames]
#
# Divergences here are places where the client (TFCC *and* upstream
# tf15-client) predicts differently from the stock server.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${TFC_SO:?set TFC_SO}"
OUT=${PM_TEST_OUT:-$ROOT/build/pmtest-tfcso}
mkdir -p "$OUT"
INC="-I$ROOT/common -I$ROOT/engine -I$ROOT/pm_shared -I$ROOT/public"
gcc -m32 -O3 -march=pentium-m -mfpmath=387 -fno-strict-aliasing -fPIC -shared $INC \
	-o "$OUT/libpm_tfcc.so" "$ROOT/pm_shared/pm_shared.c" "$ROOT/pm_shared/pm_math.c" "$ROOT/pm_shared/pm_debug.c" \
	-lm -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp
gcc -m32 -O1 $INC -o "$OUT/pm_diff" "$ROOT/tests/pm/pm_diff.c" -ldl -lm
cp "$TFC_SO" "$OUT/tfc.so"
# Valve's tfc.so keeps PM_* and pmove in .symtab only; hand their offsets over.
sym() { nm "$OUT/tfc.so" | awk -v n="$1" '$3==n {print "0x"$1; exit}'; }
anchor=GiveFnptrsToDll
PM_SYMTAB="$anchor=$(sym $anchor)"
for n in PM_Init PM_Move PM_FlyMove pmove; do v=$(sym $n); echo "tfc.so $n=$v"; PM_SYMTAB="$PM_SYMTAB,$n=$v"; done
export PM_SYMTAB
cd "$(dirname "$TFC_SO")/../.."   # tfc.so may want to find its neighbours
PM_TOL=${PM_TOL:-0.01} LD_LIBRARY_PATH="$(pwd):${LD_LIBRARY_PATH:-}" "$OUT/pm_diff" "$OUT/tfc.so" "$OUT/libpm_tfcc.so" "${1:-300}" "${2:-300}"
