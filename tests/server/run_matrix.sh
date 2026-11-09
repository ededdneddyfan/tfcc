#!/bin/bash
# Server regression matrix: load every map under two server profiles and
# check that TFCC's server bundle adds no errors, crashes or plugin failures.
#
#   HLDS=/path/to/hlds  OVERLAY=/path/to/server-config  ARTIFACTS=/path/to/bins \
#     tests/server/run_matrix.sh [map ...]
#
#   HLDS       HLDS install with tfc/ (steamcmd +app_update 90 +app_set_config 90 mod tfc)
#   OVERLAY    server config to test with: a folder holding addons/ (metamod,
#              amxmodx, plugins) and optionally server.cfg, maps/, models/,
#              sound/, sprites/, gfx/ ... (e.g. tfc-pickup-server-configuration
#              plus any custom content). Copied over tfc/.
#   ARTIFACTS  folder with tfcc_mm_i386.so, tfc_fixes_mm_mm_i386.so (+tfc_fixes.ini),
#              optional otfc_movefix_mm_i386.so, and tfcc_selftest.amxx
#   MAP_SECONDS seconds of bot play per map (default 45)
#   OUT        results folder (default ./matrix-results)
#
# Profiles:
#   stock  OVERLAY as-is (+ the selftest driver plugin)
#   tfcc   stock + tfc_fixes_mm + otfc_movefix (if given) + tfcc_mm,
#          minus BugFixesTFC.amxx (tfc_fixes_mm replaces it; running both is
#          unsupported by its author)
#
# A map FAILS if under the tfcc profile it: crashes or never finishes the
# selftest; prints an error line (see FATAL_RE / ERROR_RE) that the stock
# profile did not print for the same map; has an AMXX plugin not "running"
# that was running under stock; or tfcc_mm / tfc_fixes_mm are not loaded.
set -u
: "${HLDS:?set HLDS}"; : "${OVERLAY:?set OVERLAY}"; : "${ARTIFACTS:?set ARTIFACTS}"
SECONDS_PER_MAP=${MAP_SECONDS:-45}
OUT=${OUT:-$PWD/matrix-results}
TFC=$HLDS/tfc
mkdir -p "$OUT"

FATAL_RE='FATAL ERROR|Sys_Error|Host_Error|Segmentation fault|Aborted|core dumped|Trigger in clipping list|SV_StartSound|ED_Alloc: no free edicts'
ERROR_RE='[Ee]rror|[Ff]ailed|[Cc]ouldn.t|[Cc]an.t |not found|[Bb]ad load|[Mm]issing|Warning: |overflow|Overflow'
normalize() { sed -E 's/^ *\[ *[0-9]+\]//; s/\( *[0-9]+\)//g; s/[0-9]{2}:[0-9]{2}:[0-9]{2}//g; s/0x[0-9a-fA-F]+/0xADDR/g; s/[0-9]+\.[0-9]+ (sec|ms)/N \1/g'; }

# HLDS crashes at startup unless it finds steamclient.so here.
mkdir -p "$HOME/.steam/sdk32"
[ -e "$HOME/.steam/sdk32/steamclient.so" ] || ln -sf "$HLDS/steamclient.so" "$HOME/.steam/sdk32/steamclient.so"

# append a line to a file that may lack a trailing newline
append_line() { [ -s "$2" ] && [ -n "$(tail -c1 "$2")" ] && echo >> "$2"; echo "$1" >> "$2"; }

[ -f "$TFC/liblist.gam.orig" ] || cp "$TFC/liblist.gam" "$TFC/liblist.gam.orig"
[ -d "$TFC/.pristine_addons" ] || { mkdir -p "$TFC/.pristine_addons"; cp -a "$TFC/addons" "$TFC/.pristine_addons/" 2>/dev/null || true; }

setup_profile() {
	local prof=$1
	rm -rf "$TFC/addons"
	cp -a "$OVERLAY/." "$TFC/"
	rm -rf "$TFC/.git"
	sed -E '/^gamedll_linux/d' "$TFC/liblist.gam.orig" > "$TFC/liblist.gam"
	echo 'gamedll_linux "addons/metamod/dlls/metamod.so"' >> "$TFC/liblist.gam"
	local mmini=$TFC/addons/metamod/plugins.ini
	local amxini=$TFC/addons/amxmodx/configs/plugins.ini
	cp "$ARTIFACTS/tfcc_selftest.amxx" "$TFC/addons/amxmodx/plugins/"
	grep -q tfcc_selftest "$amxini" || append_line "tfcc_selftest.amxx" "$amxini"
	if [ "$prof" = tfcc ]; then
		mkdir -p "$TFC/addons/tfcc_mm" "$TFC/addons/tfc_fixes_mm"
		cp "$ARTIFACTS/tfcc_mm_i386.so" "$TFC/addons/tfcc_mm/"
		cp "$ARTIFACTS/tfc_fixes_mm_mm_i386.so" "$TFC/addons/tfc_fixes_mm/"
		[ -f "$ARTIFACTS/tfc_fixes.ini" ] && cp "$ARTIFACTS/tfc_fixes.ini" "$TFC/addons/tfc_fixes_mm/"
		append_line "linux addons/tfc_fixes_mm/tfc_fixes_mm_mm_i386.so" "$mmini"
		if [ -f "$ARTIFACTS/otfc_movefix_mm_i386.so" ]; then
			mkdir -p "$TFC/addons/otfc_movefix"
			cp "$ARTIFACTS/otfc_movefix_mm_i386.so" "$TFC/addons/otfc_movefix/"
			append_line "linux addons/otfc_movefix/otfc_movefix_mm_i386.so" "$mmini"
		fi
		append_line "linux addons/tfcc_mm/tfcc_mm_i386.so" "$mmini"
		sed -i -E 's/^(BugFixesTFC\.amxx)/;\1 ; replaced by tfc_fixes_mm/' "$amxini"
	fi
}

run_map() {
	local prof=$1 map=$2
	local log=$OUT/$prof/$map.log
	mkdir -p "$OUT/$prof"
	( cd "$HLDS" && LD_LIBRARY_PATH=".:$HLDS" timeout --signal=KILL $((SECONDS_PER_MAP + 60)) \
		./hlds_linux -game tfc -norestart -nomaster +sv_lan 1 +maxplayers 16 +port $((27015 + RANDOM % 1000)) \
		+localinfo tfcc_selftest 1 +localinfo tfcc_selftest_seconds "$SECONDS_PER_MAP" \
		+map "$map" < /dev/null > "$log" 2>&1 )
	local rc=$?
	echo $rc > "$log.exit"
	# Steam init occasionally fails on a cold runner; that is the environment,
	# not the map or TFCC. Retry such runs (up to twice).
}

maps=("$@")
if [ ${#maps[@]} -eq 0 ]; then
	for f in "$TFC"/maps/*.bsp "$OVERLAY"/maps/*.bsp; do [ -f "$f" ] && maps+=("$(basename "$f" .bsp)"); done
	mapfile -t maps < <(printf '%s\n' "${maps[@]}" | sort -u)
fi
echo "maps: ${#maps[@]}"

for prof in stock tfcc; do
	setup_profile $prof
	for m in "${maps[@]}"; do
		echo "[$prof] $m"
		for attempt in 1 2 3; do
			run_map $prof "$m"
			# Steam init sometimes fails on a cold runner: environment, not map/TFCC
			grep -q 'Unable to initialize Steam' "$OUT/$prof/$m.log" || break
			echo "  steam init failed, retrying ($attempt)"; sleep 2
		done
	done
done

# restore
rm -rf "$TFC/addons"; cp -a "$TFC/.pristine_addons/addons" "$TFC/" 2>/dev/null || true
cp "$TFC/liblist.gam.orig" "$TFC/liblist.gam"

fail=0
report=$OUT/report.md
{
echo "| map | stock | tfcc | verdict | notes |"
echo "|---|---|---|---|---|"
} > "$report"
for m in "${maps[@]}"; do
	s=$OUT/stock/$m.log; t=$OUT/tfcc/$m.log; notes=""; verdict=PASS
	sdone=$(grep -c 'TFCC_SELFTEST done' "$s"); tdone=$(grep -c 'TFCC_SELFTEST done' "$t")
	[ "$tdone" = 0 ] && { verdict=FAIL; notes="$notes tfcc run did not finish;"; }
	if grep -qE "$FATAL_RE" "$t"; then verdict=FAIL; notes="$notes fatal: $(grep -E "$FATAL_RE" "$t" | head -1 | tr '|' '/');"; fi
	newerr=$(comm -13 <(grep -E "$ERROR_RE" "$s" | normalize | sort -u) <(grep -E "$ERROR_RE" "$t" | normalize | sort -u) \
		| grep -vE 'BugFixesTFC' || true)
	[ -n "$newerr" ] && { verdict=FAIL; notes="$notes new errors: $(echo "$newerr" | head -3 | tr '\n|' '; ')"; }
	# AMXX plugins running under stock but not under tfcc
	srun=$(grep -E '^\[ *[0-9]+\].* running *$' "$s" | grep -oE '[A-Za-z0-9_.-]+\.amxx' | sort -u)
	trun=$(grep -E '^\[ *[0-9]+\].* running *$' "$t" | grep -oE '[A-Za-z0-9_.-]+\.amxx' | sort -u)
	lost=$(comm -23 <(echo "$srun") <(echo "$trun") | grep -v BugFixesTFC || true)
	[ -n "$lost" ] && { verdict=FAIL; notes="$notes plugins not running: $(echo $lost);"; }
	grep -q 'TFCC companion' "$t" || { verdict=FAIL; notes="$notes tfcc_mm not loaded;"; }
	grep -qiE 'TFC Fixes' "$t" || { verdict=FAIL; notes="$notes tfc_fixes_mm not loaded;"; }
	[ "$sdone" = 0 ] && notes="$notes (stock run also did not finish: map/config problem, not TFCC);"
	[ $verdict = FAIL ] && fail=1
	echo "| $m | $( [ "$sdone" = 1 ] && echo ok || echo 'no finish') | $( [ "$tdone" = 1 ] && echo ok || echo 'no finish') | $verdict | $notes |" >> "$report"
done
cat "$report"
# Show the logs of the first failing map in the job output (artifact storage
# isn't always reachable from where results are read).
first=$(awk -F'|' '$5 ~ /FAIL/ {gsub(/ /,"",$2); print $2; exit}' "$report")
if [ -n "$first" ]; then
	for prof in stock tfcc; do
		echo "===== $prof/$first.log (exit $(cat "$OUT/$prof/$first.log.exit" 2>/dev/null)) ====="
		grep -E 'META|AMX|tfcc|TFCC|TFC Fixes|OTFC|rror|ault' "$OUT/$prof/$first.log" | head -60
		echo "----- tail -----"; tail -40 "$OUT/$prof/$first.log"
	done
fi
[ $fail = 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $fail
