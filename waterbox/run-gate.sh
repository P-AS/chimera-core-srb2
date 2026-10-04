#!/bin/sh
# run-gate.sh - the gate: every leg says what it compared.
#
# usage: waterbox/run-gate.sh [-d <SRB2 data folder>]
#   -d   the folder holding srb2.pk3, zones.pk3, characters.pk3 and music.pk3
#        (2.2.15's); default /usr/share/games/SRB2. The data is never the
#        core's to carry: the gate links it into build/gate/work.
#
# Legs:
#   time    the machine's clock is its own: a run with the host stalled 300 ms
#           mid-run is the run without, picture for picture and tic for tic;
#           and the teeth - on the host's clock (--host-clock) the same stall
#           changes the run, so the comparison can fail
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
data=/usr/share/games/SRB2
while getopts d: o; do
	case "$o" in
	d) data="$OPTARG" ;;
	*) sed -n '2,8p' "$0" >&2; exit 2 ;;
	esac
done

native="$root/build/native/run-native"
[ -x "$native" ] || { echo "build the native reference first: make -C waterbox -f native.mk" >&2; exit 1; }

work="$root/build/gate/work"
mkdir -p "$work"
for f in srb2.pk3 zones.pk3 characters.pk3 music.pk3; do
	[ -f "$data/$f" ] || { echo "no $f in $data (-d names the SRB2 data folder)" >&2; exit 1; }
	ln -sf "$data/$f" "$work/$f"
done
echo "data: $data"

fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

# the run's last line: the hash of every step's picture, and the tic
run() { "$native" "$work" -p 0 "$@" 2>/dev/null | tail -1; }

# ---- time: 700 steps (the intro's wipes and its in-tic waits), stalled at 350
steps=700
base="$(run -n $steps)"
stalled="$(run -n $steps --stall-at 350 --stall-ms 300)"
if [ "$base" = "$stalled" ]; then
	pass "time: a 300 ms host stall at step 350 of $steps changes nothing ($base)"
else
	bad "time: the stall changed the run: '$base' != '$stalled'"
fi
host_base="$(run -n $steps --host-clock)"
host_stalled="$(run -n $steps --host-clock --stall-at 350 --stall-ms 300)"
if [ "$host_base" != "$host_stalled" ]; then
	pass "time teeth: on the host's clock the same stall changes the run ('$host_base' != '$host_stalled')"
else
	bad "time teeth: the host's clock did not notice the stall either - the leg cannot fail"
fi

[ "$fail" -eq 0 ] && echo "gate: all legs pass" || echo "gate: FAILED"
exit "$fail"
