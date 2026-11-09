#!/bin/sh
# ABI / protocol compatibility checks for the TFCC client.
#
#   tests/abi/check.sh <tfcc client.so>                 # required exports + message list
#   STOCK_CLIENT=/path/to/stock/client.so tests/abi/check.sh <tfcc client.so>
#
# 1. Every engine entry point in client_exports.txt is exported.
# 2. With STOCK_CLIENT: every symbol the stock TFC client exports is exported.
# 3. With STOCK_CLIENT: every user message the stock client hooks is hooked by
#    TFCC (names found as strings in the stock binary vs HOOK_MESSAGE in
#    source). A missing hook would make a server/AMXX message silently ignored.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CLIENT=${1:?usage: check.sh <client.so>}
fail=0

exports() { nm -D --defined-only "$1" | awk '$2 ~ /^[TtWiV]$/ {print $3}' | sed 's/@.*//' | sort -u; }

ours=$(mktemp); exports "$CLIENT" > "$ours"
missing=$(grep -v '^#' "$HERE/client_exports.txt" | grep . | sort -u | comm -23 - "$ours")
if [ -n "$missing" ]; then echo "FAIL required exports missing:"; echo "$missing" | sed 's/^/  /'; fail=1
else echo "OK   all $(grep -vc '^#' "$HERE/client_exports.txt") engine entry points exported"; fi

# user messages hooked in source
hooks=$(mktemp)
grep -rhoE 'HOOK_MESSAGE\(\s*[A-Za-z0-9_]+\s*\)|gEngfuncs\.pfnHookUserMsg\(\s*"[A-Za-z0-9_]+"' "$ROOT/cl_dll" "$ROOT/game_shared" 2>/dev/null \
	| sed -E 's/.*\(\s*"?([A-Za-z0-9_]+)"?\s*\)?/\1/' | sort -u > "$hooks"
echo "OK   $(wc -l < "$hooks") user messages hooked by TFCC"

if [ -n "${STOCK_CLIENT:-}" ]; then
	stock=$(mktemp); exports "$STOCK_CLIENT" > "$stock"
	m2=$(comm -23 "$stock" "$ours" | grep -vE '^(_init|_fini|_edata|_end|__bss_start)$' || true)
	if [ -n "$m2" ]; then echo "FAIL symbols exported by stock client but not TFCC:"; echo "$m2" | sed 's/^/  /'; fail=1
	else echo "OK   TFCC exports every symbol the stock client exports ($(wc -l < "$stock"))"; fi
	# message names: stock binary stores them as strings next to __MsgFunc_ symbols
	smsg=$(mktemp)
	nm -D "$STOCK_CLIENT" 2>/dev/null | grep -oE '__MsgFunc_[A-Za-z0-9_]+' | sed 's/^__MsgFunc_//' | sort -u > "$smsg"
	if [ ! -s "$smsg" ]; then nm "$STOCK_CLIENT" 2>/dev/null | grep -oE '__MsgFunc_[A-Za-z0-9_]+' | sed 's/^__MsgFunc_//' | sort -u > "$smsg"; fi
	if [ -s "$smsg" ]; then
		m3=$(comm -23 "$smsg" "$hooks")
		if [ -n "$m3" ]; then echo "FAIL user messages hooked by stock client but not TFCC:"; echo "$m3" | sed 's/^/  /'; fail=1
		else echo "OK   TFCC hooks every user message the stock client hooks ($(wc -l < "$smsg"))"; fi
		extra=$(comm -13 "$smsg" "$hooks")
		[ -n "$extra" ] && echo "INFO TFCC hooks messages stock does not: $(echo $extra)"
	else
		echo "SKIP stock client has no __MsgFunc_ symbols (stripped); message check skipped"
	fi
fi
[ $fail = 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $fail
