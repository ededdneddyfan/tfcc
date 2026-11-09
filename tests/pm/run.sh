#!/bin/sh
# Differential movement test: baseline pm_shared (upstream) vs TFCC pm_shared.
#
#   tests/pm/run.sh [BASELINE_REF] [scenarios] [frames]
#
# BASELINE_REF defaults to the merge-base with the upstream branch (main).
# Builds both copies with the same flags as the GoldSrc client release
# (x87 math, -O3) and once more with SSE math, and runs pm_diff on each.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
REF=${1:-$(git -C "$ROOT" merge-base HEAD origin/main 2>/dev/null || git -C "$ROOT" merge-base HEAD main)}
SCEN=${2:-1500}
FRAMES=${3:-300}
OUT=${PM_TEST_OUT:-$ROOT/build/pmtest}
CC=${CC:-gcc}
rm -rf "$OUT"; mkdir -p "$OUT/base/pm_shared"
git -C "$ROOT" show "$REF:pm_shared/pm_shared.c" > "$OUT/base/pm_shared/pm_shared.c"
git -C "$ROOT" show "$REF:pm_shared/pm_math.c" > "$OUT/base/pm_shared/pm_math.c"
git -C "$ROOT" show "$REF:pm_shared/pm_debug.c" > "$OUT/base/pm_shared/pm_debug.c"
INC="-I$ROOT/common -I$ROOT/engine -I$ROOT/pm_shared -I$ROOT/public"
status=0
for mode in x87 sse; do
	case $mode in
	x87) FLAGS="-m32 -O3 -march=pentium-m -mfpmath=387 -fno-strict-aliasing" ;;
	sse) FLAGS="-m32 -O2 -msse2 -mfpmath=sse" ;;
	esac
	for side in base tfcc; do
		src=$ROOT/pm_shared; [ $side = base ] && src=$OUT/base/pm_shared
		$CC $FLAGS -fPIC -shared $INC -o "$OUT/libpm_${side}_$mode.so" \
			"$src/pm_shared.c" "$src/pm_math.c" "$src/pm_debug.c" -lm \
			-Dstricmp=strcasecmp -Dstrnicmp=strncasecmp
	done
	$CC -m32 -O1 $INC -o "$OUT/pm_diff" "$ROOT/tests/pm/pm_diff.c" -ldl -lm
	echo "== pm_diff ($mode, baseline $REF) =="
	"$OUT/pm_diff" "$OUT/libpm_base_$mode.so" "$OUT/libpm_tfcc_$mode.so" "$SCEN" "$FRAMES" || status=1
done
exit $status
