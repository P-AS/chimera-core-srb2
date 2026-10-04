#!/bin/sh
# run-gate.sh - the gate: every leg says what it compared.
#
# usage: waterbox/run-gate.sh [-d <SRB2 data folder>] [-m <miniBox checkout>]
#   -d   the folder holding srb2.pk3, zones.pk3, characters.pk3 and music.pk3
#        (2.2.15's); default /usr/share/games/SRB2. The data is never the
#        core's to carry: the gate links it into build/gate/.
#   -m   miniBox (default $MINIBOX_DIR): the sandbox legs run core.wbx through
#        its host (build/native/run-wbx)
#
# The content: the game's start (the intro, 700 steps: its wipes and in-tic
# waits) and Greenflower Zone Act 1 (-warp 1, 1000 steps: the level, its
# enemies, the HUD, Lua). No input yet.
#
# Legs:
#   equivalence  native == sandbox: every step's picture, the engine's tic and
#                the machine's clock, on both contents; its teeth - the native
#                build on the host's clock is not the sandbox
#   time         the machine's clock is its own: a 300 ms host stall mid-run
#                changes nothing, native and sandbox; its teeth - on the host's
#                clock the same stall changes the run
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
data=/usr/share/games/SRB2
mb="${MINIBOX_DIR:-}"
while getopts d:m: o; do
	case "$o" in
	d) data="$OPTARG" ;;
	m) mb="$OPTARG" ;;
	*) sed -n '2,10p' "$0" >&2; exit 2 ;;
	esac
done

native="$root/build/native/run-native"
wbxhost="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"
for f in "$native" "$wbxhost"; do
	[ -x "$f" ] || { echo "build the native reference first: make -C waterbox -f native.mk MINIBOX_DIR=<miniBox>" >&2; exit 1; }
done
[ -f "$core" ] || { echo "build the core first: make -C waterbox -f guest.mk MINIBOX_DIR=<miniBox>" >&2; exit 1; }

# a work folder per content: the data linked in, and its settings
content() {
	dir="$root/build/gate/$1"
	mkdir -p "$dir"
	for f in srb2.pk3 zones.pk3 characters.pk3 music.pk3; do
		[ -f "$data/$f" ] || { echo "no $f in $data (-d names the SRB2 data folder)" >&2; exit 1; }
		ln -sf "$data/$f" "$dir/$f"
	done
	printf '%s\n' "$2" > "$dir/settings"
}
content intro '{}'
content gfz1 '{"warp": "1"}'
echo "data: $data"
echo "core: $core"

fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

# a run's step lines and its last line ("run <hash> tic <n> clock <c>")
nat() { w="$1"; shift; "$native" "$root/build/gate/$w" "$@" 2>/dev/null | grep -E '^(step|run) '; }
box() { w="$1"; shift; "$wbxhost" "$core" "$root/build/gate/$w" "$@" 2>/dev/null | grep -E '^(step|run) '; }

# ---- equivalence
for c in "intro 700" "gfz1 1000"; do
	set -- $c
	n="$(nat "$1" -n "$2" -p 50)"
	b="$(box "$1" -n "$2" -p 50)"
	if [ -n "$n" ] && [ "$n" = "$b" ]; then
		pass "equivalence: $1, $2 steps: native == sandbox ($(echo "$b" | tail -1))"
	else
		bad "equivalence: $1, $2 steps: native != sandbox"
		echo "$n" > "$root/build/gate/$1-native.txt"
		echo "$b" > "$root/build/gate/$1-sandbox.txt"
		echo "  (build/gate/$1-native.txt, build/gate/$1-sandbox.txt)"
	fi
done
n="$(nat gfz1 -n 200 -p 50 --host-clock)"
b="$(box gfz1 -n 200 -p 50)"
if [ "$n" != "$b" ]; then
	pass "equivalence teeth: the native build on the host's clock is not the sandbox"
else
	bad "equivalence teeth: the host's clock made no difference - the leg cannot fail"
fi

# ---- time: the intro (its wipes and in-tic waits), stalled half way
steps=700
base="$(nat intro -n $steps -p 0)"
stalled="$(nat intro -n $steps -p 0 --stall-at 350 --stall-ms 300)"
[ "$base" = "$stalled" ] && pass "time: native: a 300 ms host stall at step 350 changes nothing" \
	|| bad "time: native: the stall changed the run: '$base' != '$stalled'"
bbase="$(box intro -n $steps -p 0)"
bstalled="$(box intro -n $steps -p 0 --stall-at 350 --stall-ms 300)"
[ "$bbase" = "$bstalled" ] && pass "time: sandbox: a 300 ms host stall at step 350 changes nothing" \
	|| bad "time: sandbox: the stall changed the run: '$bbase' != '$bstalled'"
hbase="$(nat intro -n $steps -p 0 --host-clock)"
hstalled="$(nat intro -n $steps -p 0 --host-clock --stall-at 350 --stall-ms 300)"
[ "$hbase" != "$hstalled" ] && pass "time teeth: on the host's clock the same stall changes the run" \
	|| bad "time teeth: the host's clock did not notice the stall either - the leg cannot fail"

[ "$fail" -eq 0 ] && echo "gate: all legs pass" || echo "gate: FAILED"
exit "$fail"
