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
# The content: the game's start (the intro, 700 steps: its wipes, a frame a
# step) and Greenflower Zone Act 1 (-warp 1: its entry - the fade, the title
# card - and play: the level, its enemies, the HUD, Lua). No input yet.
#
# Legs:
#   equivalence  native == sandbox: every step's picture, the engine's tic, the
#                machine's clock and the lag count, on both contents; its teeth -
#                the native build on the host's clock is not the sandbox
#   steps        a step is a tic: every step moves the machine's clock exactly
#                one tic; the level's entry (the fade, the title card, the fade
#                in) is a run of lag steps with the game frozen, a frame of the
#                wipe each; in play every step reads input and runs one tic. Its
#                teeth - on the host's clock (where a sleep sleeps and a wipe
#                passes inside one step) the same checks fail
#   savestates   the sandbox's machine saved and loaded before every step
#                (rerecord), and moved to a new host mid-wipe (session: the
#                engine suspended on its own cothread), is the run without; its
#                teeth - a stale state (a step run twice) is not
#   input        the controller: waterbox/tests/menu-to-tutorial.txt (Enter
#                through the intro, the title, the menus, into the Tutorial
#                Zone) and gfz1-run.txt (Forward, Jump, the Turn axis in
#                Greenflower); each the same native, sandboxed, rerecorded and
#                in a new host. Its teeth - the run without the input is not
#   audio        the core's mixer: the intro's music and Greenflower's sounds are
#                heard (not silence), and the same native and sandboxed (every
#                leg compares the sound too: the run line carries its hash); a
#                GME song (GME's own test.nsf, from SRB2's libs/gme) and a
#                tracker module (libopenmpt's own test.mod), each in a PWAD the
#                gate makes and played with the console's tunes, the same
#                native, sandboxed and rerecorded. Its teeth - the Greenflower
#                run without its jump, and the start without the songs, do not
#                sound the same
#   files        the machine's filesystem: a mounted autoexec.cfg writes a file
#                at start (before seal) and one 20 tics into play (after it),
#                and reads the first back; the save data export is those two
#                files, byte for byte the same native, sandboxed, rerecorded
#                and moved to a new host, and nothing is written to the host's
#                work folder. Its teeth - a run that ends before the second
#                write does not pass
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
content files '{}'
content gme '{}'
python3 "$here/tests/make-wad.py" "$root/build/gate/gme/gmetest.wad" O_GMETST="$root/extern/SRB2/libs/gme/test.nsf"
printf 'addfile gmetest.wad\ntunes gmetst\n' > "$root/build/gate/gme/autoexec.cfg"
content mod '{}'
python3 "$here/tests/make-wad.py" "$root/build/gate/mod/modtest.wad" O_MODTST="$root/extern/openmpt/test/test.mod"
printf 'addfile modtest.wad\ntunes modtst\n' > "$root/build/gate/mod/autoexec.cfg"
printf 'saveconfig mine.cfg\nexec mine.cfg\nwait 20\nsaveconfig late.cfg\n' > "$root/build/gate/files/autoexec.cfg"
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

# ---- steps: on Greenflower Zone Act 1's entry and its first seconds of play.
# The step lines: "step <n> tic <t> clock <c> lag <l> picture <p>".
# Checked: every clock delta is one tic (1000000 units); at least 40 lag steps
# before step 70, none of which runs a tic; from step 80, every step reads
# input and runs exactly one tic.
steps_check() {
	awk '/^step/ {
		n = $2; t = $4; c = $6; l = $8
		if (n > 1) {
			if (c - pc != 1000000) clock++
			if (l > pl && t != pt) lagrun++
			if (n <= 70 && l > pl) entry++
			if (n >= 80 && (l != pl || t != pt + 1)) play++
		}
		pc = c; pt = t; pl = l
	}
	END {
		if (clock || lagrun || entry < 40 || play) {
			printf "clock-steps-not-one-tic=%d lag-steps-running-tics=%d entry-lag=%d play-steps-not-one-tic=%d\n", clock, lagrun, entry, play
			exit 1
		}
		printf "every step one tic of clock; %d lag steps on the entry, the game frozen; play one tic a step\n", entry
	}'
}
if r="$(nat gfz1 -n 200 -p 1 | steps_check)"; then pass "steps: native: $r"; else bad "steps: native: $r"; fi
if r="$(box gfz1 -n 200 -p 1 | steps_check)"; then pass "steps: sandbox: $r"; else bad "steps: sandbox: $r"; fi
if r="$(nat gfz1 -n 200 -p 1 --host-clock | steps_check)"; then
	bad "steps teeth: the host's clock passed the checks too - the leg cannot fail ($r)"
else
	pass "steps teeth: on the host's clock the checks fail ($r)"
fi

# ---- savestates: 150 steps of Greenflower's entry (the wipe, the title card)
# and play; the session moves at step 30, mid-wipe
plain="$(box gfz1 -n 150 -p 10)"
rr="$(box gfz1 -n 150 -p 10 --rerecord)"
[ -n "$plain" ] && [ "$plain" = "$rr" ] && pass "savestates: rerecord (a state saved and loaded before every step) is the run without" \
	|| bad "savestates: rerecord changed the run"
ss="$(box gfz1 -n 150 -p 10 --session-at 30)"
[ "$plain" = "$ss" ] && pass "savestates: session (a new host at step 30, mid-wipe) is the run without" \
	|| bad "savestates: the session changed the run"
stale="$(box gfz1 -n 150 -p 10 --stale-state 100)"
[ "$plain" != "$stale" ] && pass "savestates teeth: a stale state (step 100 run twice) changes the run" \
	|| bad "savestates teeth: a stale state changed nothing - the legs cannot fail"

# ---- input
tests="$here/tests"
for m in "intro menu-to-tutorial 450" "gfz1 gfz1-run 260"; do
	set -- $m
	n="$(nat "$1" -n "$3" -p 25 --input "$tests/$2.txt")"
	b="$(box "$1" -n "$3" -p 25 --input "$tests/$2.txt")"
	r="$(box "$1" -n "$3" -p 25 --input "$tests/$2.txt" --rerecord)"
	s2="$(box "$1" -n "$3" -p 25 --input "$tests/$2.txt" --session-at 150)"
	if [ -n "$n" ] && [ "$n" = "$b" ] && [ "$n" = "$r" ] && [ "$n" = "$s2" ]; then
		pass "input: $2: native == sandbox == rerecord == session ($(echo "$n" | tail -1))"
	else
		bad "input: $2: the runs differ"
	fi
	none="$(nat "$1" -n "$3" -p 25)"
	[ "$n" != "$none" ] && pass "input teeth: $2: the run without the input is not the run with it" \
		|| bad "input teeth: $2: the input changed nothing - the leg cannot fail"
done

# ---- audio
field() { echo "$1" | tail -1 | awk -v k="$2" '{for (i = 1; i < NF; i++) if ($i == k) print $(i + 1)}'; }
ni="$(nat intro -n 700 -p 0)"
bi="$(box intro -n 700 -p 0)"
ng="$(nat gfz1 -n 200 -p 0 --input "$tests/gfz1-run.txt")"
if [ "$(field "$ni" peak)" -gt 1000 ] && [ "$(field "$ng" peak)" -gt 1000 ] \
	&& [ "$(field "$ni" audio)" = "$(field "$bi" audio)" ]; then
	pass "audio: the intro's music (peak $(field "$ni" peak)) and Greenflower's sounds (peak $(field "$ng" peak)) are heard, native == sandbox"
else
	bad "audio: silent, or native != sandbox ('$ni' / '$bi' / '$ng')"
fi
printf '80-260: Forward\n180-220: Turn=-600\n' > "$root/build/gate/gfz1-nojump.txt"
nj="$(nat gfz1 -n 200 -p 0 --input "$root/build/gate/gfz1-nojump.txt")"
[ "$(field "$nj" audio)" != "$(field "$ng" audio)" ] && pass "audio teeth: without its jump, Greenflower does not sound the same" \
	|| bad "audio teeth: the jump changed no sound - the leg cannot fail"

for song in "gme GME song (test.nsf)" "mod tracker module (test.mod)"; do
	set -- $song
	c="$1"
	shift
	nc="$(nat "$c" -n 300 -p 10)"
	bc="$(box "$c" -n 300 -p 10)"
	rc="$(box "$c" -n 300 -p 10 --rerecord)"
	if [ "$(field "$nc" peak)" -gt 1000 ] && [ "$nc" = "$bc" ] && [ "$nc" = "$rc" ]; then
		pass "audio: a $* is heard, native == sandbox == rerecord ($(echo "$nc" | tail -1))"
	else
		bad "audio: the $* is silent or differs ('$(echo "$nc" | tail -1)' / '$(echo "$bc" | tail -1)' / '$(echo "$rc" | tail -1)')"
	fi
	[ "$(field "$nc" audio)" != "$(field "$ni" audio)" ] && pass "audio teeth: the start with the $* does not sound like the start without it" \
		|| bad "audio teeth: the $* changed no sound - the leg cannot fail"
done

# ---- files: the save data export of 200 steps, every way
sd="$root/build/gate/savedata"
rm -rf "$sd"
before="$(ls -A "$root/build/gate/files")"
nat files -n 200 -p 0 --savedata-out "$sd/native" >/dev/null
box files -n 200 -p 0 --savedata-out "$sd/sandbox" >/dev/null
box files -n 200 -p 0 --rerecord --savedata-out "$sd/rerecord" >/dev/null
box files -n 200 -p 0 --session-at 100 --savedata-out "$sd/session" >/dev/null
box files -n 50 -p 0 --savedata-out "$sd/short" >/dev/null
listing() { (cd "$sd/$1" 2>/dev/null && find . -type f | sort | xargs -r md5sum); }
want="$(printf '%s\n' ./late.cfg ./mine.cfg)"
names() { listing "$1" | awk '{print $2}'; }
n="$(listing native)"
if [ "$(names native)" = "$want" ] && [ "$n" = "$(listing sandbox)" ] && [ "$n" = "$(listing rerecord)" ] \
	&& [ "$n" = "$(listing session)" ] && [ "$before" = "$(ls -A "$root/build/gate/files")" ]; then
	pass "files: the export (mine.cfg before seal, late.cfg after) is the same native, sandboxed, rerecorded and in a new host; the host's folder untouched"
else
	bad "files: the exports differ, or are not mine.cfg and late.cfg, or the host's folder changed (build/gate/savedata)"
fi
[ "$(names short)" != "$want" ] && pass "files teeth: a run ending before the second write does not pass" \
	|| bad "files teeth: the short run passed - the leg cannot fail"

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
