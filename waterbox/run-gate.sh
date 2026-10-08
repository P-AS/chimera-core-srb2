#!/bin/sh
# run-gate.sh - the gate: every leg says what it compared.
#
# usage: waterbox/run-gate.sh [-d <SRB2 data folder>] [-m <miniBox checkout>] [-c <Chimera bundle>]
#   -d   the folder holding srb2.pk3, zones.pk3, characters.pk3 and music.pk3
#        (2.2.15's); default /usr/share/games/SRB2. The data is never the
#        core's to carry: the gate links it into build/gate/.
#   -m   miniBox (default $MINIBOX_DIR): the sandbox legs run core.wbx through
#        its host (build/native/run-wbx)
#   -c   a Chimera bundle (the folder with Chimera.exe), or a Chimera checkout's
#        installed build/ (meson install --libdir dll): the engine leg opens
#        the package through its dll/libchimera.so, as the frontend does
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
#   input        the controller: waterbox/tests/menu-to-new-game.txt (Enter
#                through the intro, the title, the menus, into a new game) and gfz1-run.txt (Forward, Jump, the Turn axis in
#                Greenflower); each the same native, sandboxed, rerecorded and
#                in a new host. Its teeth - the run without the input is not
#   properties   the Game State domain, read by its property table as Chimera
#                reads it (RAM Watch, Lua's game.get): on Greenflower's movie
#                the player's position, momentum, angle and speed are what the
#                movie did (moved by Forward, lifted by Jump, turned only by
#                Turn; in the air, speed is the momentum's P_AproxDistance), and
#                with tests/timers.lua loaded the speed shoes, invincibility,
#                air and space timers and the conveyor and platform momenta are
#                what it set; on tests/spindash.txt the spindash charges from Min
#                Dash a tic at a time while Spin is held (Charging Spindash, a
#                bit of Player.Flags) and launches at its charge; every step's
#                block the same native, sandboxed and rerecorded. Its teeth -
#                the table with two fields swapped (angle and speed; air and
#                space; dash speed and max dash) does not pass
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
#   exports      core.wbx exports every call Chimera's engine requires of it
#                (source/engine/source/session.cpp's required proc() lookups:
#                Init, FrameAdvance, SetAxis, the five memory-domain calls, and
#                the getters waterbox.config names for video, audio and lag);
#                its teeth - a name the core does not export is reported missing
#   declaration  waterbox.config's controller is the core's (its buttons and
#                axes, in order, as GetButtonName/GetAxisName give them); its
#                teeth - a declaration with two buttons swapped does not pass
#   settings     the declared settings reach the engine as its options: with no
#                setting given, and with every declared default given, the
#                engine plays Manual (directionchar Movement, configanalog
#                Off), cam_speed 1.0, cam_dist 192, cam_height 40, timerres Mania, flipcam Yes, autobrake On, tutorialprompt
#                Off, nothing unlocked, Sonic; each other
#                value of each setting is the engine's option (read back with
#                run-native --print-cvar). Its teeth - another value is not the
#                default's
#   resolution   the resolution setting is the picture's alone: Greenflower's
#                movie at 320x200, 1280x800, 1920x1080 and 3840x2160, native and
#                sandboxed, plays one game (the state digest, every step) and
#                one sound, and every resolution draws its own picture; its
#                teeth - another input is another state
#   opengl       the OpenGL renderer (renderer opengl: SRB2's own, on the Mesa
#                softpipe the core carries), sandboxed, on Greenflower's movie
#                at 320x200: OpenGL started and drew a lit picture, not the
#                software renderer's; two runs are the same run (every step's
#                picture, the sound, the state); a state saved and loaded
#                before every step (rerecord), and a new host mid-wipe, are
#                the run without - every byte of the GL is the machine's. The
#                native reference, which has no GL, draws in software and says
#                so. Its teeth - a stale state (a step run twice) is not the run
#   engine       (with -c) Chimera's own engine opens the package, as the
#                frontend's session does (the required exports, the declaration,
#                the firmware, Init), and runs the menus-to-new-game movie's
#                presses for 450 steps: the same lag count as run-native's.
#                opengl-hw with no bridge asked for is the Mesa's run, and says
#                so; where the machine gives the engine a GL context, opengl-hw
#                through the bridge is the Mesa's game (the same sound), and a
#                rewind and a reopen make the renderer again and draw the
#                straight run (tests/engine-gpu-states.py, whose teeth - the
#                core built without the rebuild - were watched failing)
#   time         the machine's clock is its own: a 300 ms host stall mid-run
#                changes nothing, native and sandbox; its teeth - on the host's
#                clock the same stall changes the run
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
data=/usr/share/games/SRB2
mb="${MINIBOX_DIR:-}"
bundle=""
while getopts d:m:c: o; do
	case "$o" in
	d) data="$OPTARG" ;;
	m) mb="$OPTARG" ;;
	c) bundle="$OPTARG" ;;
	*) sed -n '2,10p' "$0" >&2; exit 2 ;;
	esac
done

# absolute: the work folders link the data in, and a relative link resolves
# from the link's own folder
[ -d "$data" ] || { echo "no data folder at $data (-d; waterbox/fetch-data.sh makes one)" >&2; exit 1; }
data="$(cd "$data" && pwd)"

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
content props '{"warp": "1"}'
cp "$here/tests/timers.lua" "$root/build/gate/props/timers.lua"
printf 'addfile timers.lua\n' > "$root/build/gate/props/autoexec.cfg"
python3 "$here/tests/make-wad.py" "$root/build/gate/mod/modtest.wad" O_MODTST="$root/extern/openmpt/test/test.mod"
printf 'addfile modtest.wad\ntunes modtst\n' > "$root/build/gate/mod/autoexec.cfg"
printf 'saveconfig mine.cfg\nexec mine.cfg\nwait 20\nsaveconfig late.cfg\n' > "$root/build/gate/files/autoexec.cfg"
echo "data: $data"
echo "core: $core"

fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

# a run's step lines and its last line ("run <hash> tic <n> clock <c>")
# a run that prints nothing (a crash, a missing file) is an empty answer the
# legs then fail on - never a silent end of the gate (set -e)
nat() { w="$1"; shift; "$native" "$root/build/gate/$w" "$@" 2>/dev/null | grep -E '^(step|run) ' || true; }
box() { w="$1"; shift; "$wbxhost" "$core" "$root/build/gate/$w" "$@" 2>/dev/null | grep -E '^(step|run) ' || true; }

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
for m in "intro menu-to-new-game 450" "gfz1 gfz1-run 260"; do
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

# ---- properties: the Game State domain, by its property table
g="$root/build/gate"
nat gfz1 -n 400 -p 0 --input "$tests/gfz1-run.txt" --game-state "$g/props-movie-native.bin" >/dev/null
box gfz1 -n 400 -p 0 --input "$tests/gfz1-run.txt" --game-state "$g/props-movie-sandbox.bin" >/dev/null
box gfz1 -n 400 -p 0 --input "$tests/gfz1-run.txt" --rerecord --game-state "$g/props-movie-rerecord.bin" >/dev/null
nat props -n 260 -p 0 --input "$tests/gfz1-run.txt" --game-state "$g/props-timers-native.bin" >/dev/null
box props -n 260 -p 0 --input "$tests/gfz1-run.txt" --game-state "$g/props-timers-sandbox.bin" >/dev/null
nat gfz1 -n 160 -p 0 --input "$tests/spindash.txt" --game-state "$g/props-spindash-native.bin" >/dev/null
box gfz1 -n 160 -p 0 --input "$tests/spindash.txt" --game-state "$g/props-spindash-sandbox.bin" >/dev/null
box gfz1 -n 160 -p 0 --input "$tests/spindash.txt" --rerecord --game-state "$g/props-spindash-rerecord.bin" >/dev/null
same() { cmp -s "$1" "$2" && cmp -s "$1.json" "$2.json"; }
for kind in movie timers spindash; do
	r="$(python3 "$tests/check-properties.py" "$g/props-$kind-native.bin" "$kind" 2>&1 || true)"
	if [ "${r#ok}" != "$r" ] && same "$g/props-$kind-native.bin" "$g/props-$kind-sandbox.bin" \
		&& { [ "$kind" = timers ] || same "$g/props-$kind-native.bin" "$g/props-$kind-rerecord.bin"; }; then
		pass "properties: $r; every step's block native == sandbox$([ "$kind" = timers ] || echo ' == rerecord')"
	else
		bad "properties: $kind: $r (or the blocks differ native, sandboxed, rerecorded)"
	fi
done
for t in "movie|Player.Angle|Player.Speed" "timers|Timers.Air|Timers.Space" "spindash|Player.Dash Speed|Player.Max Dash"; do
	IFS='|' read -r kind a b <<EOT
$t
EOT
	set -- "$kind" "$a" "$b"
	r="$(python3 "$tests/check-properties.py" "$g/props-$1-native.bin" "$1" --swap "$2" "$3" 2>&1 || true)"
	case "$r" in
	FAIL*) pass "properties teeth: $2 and $3 swapped in the table: $r" ;;
	*) bad "properties teeth: $2 and $3 swapped in the table still passed: $r" ;;
	esac
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

# ---- exports: what Chimera's engine refuses to open a core without
exported="$(nm --defined-only "$core" | awk '$2 ~ /^[TtDdBb]$/ {print $3}' | sort -u)"
missing_from() {
	for name in "$@"; do
		echo "$exported" | grep -qx "$name" || printf '%s ' "$name"
	done
}
required="$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))
names=["Init","FrameAdvance","SetAxis","GetMemoryDomainCount","GetMemoryDomainName","GetMemoryDomainPtr",
 "GetMemoryDomainSize","GetMemoryDomainWritable",c["video"]["getBgra"],c["audio"]["get"],c["lag"]["inputWasRead"]]
if len(c["input"]["buttons"])>64: names.append("SetButton")
print(" ".join(names))' "$here/waterbox.config")"
m="$(missing_from $required)"
[ -z "$m" ] && pass "exports: core.wbx exports every call the engine requires ($(echo $required | wc -w))" \
	|| bad "exports: core.wbx does not export: $m"
[ -n "$(missing_from GetNothingAtAll)" ] && pass "exports teeth: a name the core does not export is reported missing" \
	|| bad "exports teeth: a missing name was not reported - the leg cannot fail"

# ---- declaration: the controller the package declares is the core's
decl="$here/waterbox.config"
declared() { python3 -c 'import json,sys; c=json.load(open(sys.argv[1])); i=c["input"]
print("\n".join(["button "+b for b in i["buttons"]]+["axis "+a["name"] for a in i["axes"]]))' "$1"; }
core_input="$("$native" --list-input)"
if [ "$(declared "$decl")" = "$core_input" ]; then
	pass "declaration: waterbox.config's controller is the core's ($(echo "$core_input" | grep -c ^button) buttons, $(echo "$core_input" | grep -c ^axis) axes, in order)"
else
	bad "declaration: waterbox.config's controller is not the core's"
fi
python3 -c 'import json,sys; c=json.load(open(sys.argv[1])); b=c["input"]["buttons"]; b[0],b[1]=b[1],b[0]; json.dump(c,open(sys.argv[2],"w"))' "$decl" "$root/build/gate/swapped.config"
[ "$(declared "$root/build/gate/swapped.config")" != "$core_input" ] && pass "declaration teeth: a declaration with two buttons swapped does not pass" \
	|| bad "declaration teeth: the swapped declaration passed - the leg cannot fail"

# ---- settings: each declared setting is the engine's option
content settings '{"warp": "1"}'
cvars() {
	printf '%s\n' "$1" > "$root/build/gate/settings/settings"
	"$native" "$root/build/gate/settings" -n 5 -p 0 --print-cvar directionchar --print-cvar configanalog \
		--print-cvar cam_speed --print-cvar cam_dist --print-cvar cam_height --print-cvar timerres --print-cvar flipcam --print-cvar autobrake --print-cvar tutorialprompt 2>/dev/null \
		| grep '^cvar' | tr '\n' ' '
}
want="cvar directionchar Movement cvar configanalog Off cvar cam_speed 1.00000 cvar cam_dist 192.00000 cvar cam_height 40.00000 cvar timerres Mania cvar flipcam Yes cvar autobrake On cvar tutorialprompt Off "
none="$(cvars '{"warp": "1"}')"
defaults="$(python3 -c 'import json,sys; c=json.load(open(sys.argv[1])); d={s["name"]:s["default"] for s in c["settings"]}; d["warp"]="1"; print(json.dumps(d))' "$decl")"
explicit="$(cvars "$defaults")"
if [ "$none" = "$want" ] && [ "$explicit" = "$want" ]; then
	pass "settings: the defaults, absent or given, are Manual, cam_speed 1.0, cam_dist 192, cam_height 40, timerres Mania, flipcam Yes, autobrake On"
else
	bad "settings: the defaults are not what they should be: absent '$none', given '$explicit', want '$want'"
fi
ok=1
for v in "playStyle Strafe directionchar Camera configanalog Off" "playStyle Automatic directionchar Movement configanalog On" \
	"playStyle 'Old Analog' directionchar Camera configanalog On" "cameraSpeed 0.3 cam_speed 0.30000" "cameraSpeed 0 cam_speed 0.00000" \
	"cameraDistance 320 cam_dist 320.00000" "cameraHeight 64.5 cam_height 64.50000" \
	"scoreTimeRings Classic timerres Classic" "scoreTimeRings Centiseconds timerres Centiseconds" "scoreTimeRings Tics timerres Tics" \
	"flipCamera false flipcam No" "autoBrake false autobrake Off"; do
	eval "set -- $v"
	key="$1"; val="$2"; shift 2
	case "$val" in [0-9]*|true|false) js="$val" ;; *) js="\"$val\"" ;; esac
	got="$(cvars "{\"warp\": \"1\", \"$key\": $js}")"
	while [ $# -gt 0 ]; do
		case "$got" in *"cvar $1 $2 "*) ;; *) ok=0; echo "  $key=$val: want $1 $2, got '$got'" ;; esac
		shift 2
	done
done
# the unlocks are the game data's (run-native --print-unlocks); none by default
unlocks() {
	printf '%s\n' "$1" > "$root/build/gate/settings/settings"
	"$native" "$root/build/gate/settings" -n 5 -p 0 --print-unlocks 2>/dev/null | grep '^unlocks'
}
for v in '{}|recordattack 0 nights 0 skins 0/' '{"unlockModes": true}|recordattack 1 nights 1 skins 0/' \
	'{"unlockCharacters": true}|recordattack 0 nights 0 skins 3/3' '{"unlockAll": true}|recordattack 1 nights 1 skins 3/3 all 24/24' \
	'{}|ramaps 0' '{"unlockMaps": true}|all 0/24 ramaps 28'; do
	got="$(unlocks "$(echo "${v%%|*}" | sed 's/^{/{"warp": "1", /; s/, }$/}/')")"
	case "$got" in *"${v#*|}"*) ;; *) ok=0; echo "  ${v%%|*}: want '${v#*|}', got '$got'" ;; esac
done
# the character Start Map plays as: the player's skin in the level; a locked
# one needs the characters unlocked, an unknown one is the game's default
skin() {
	printf '%s\n' "$1" > "$root/build/gate/settings/settings"
	"$native" "$root/build/gate/settings" -n 100 -p 0 --print-skin 2>/dev/null | grep '^skin'
}
for v in '{"warp": "1"}|sonic' '{"warp": "1", "skin": "knuckles"}|knuckles' '{"warp": "1", "skin": "amy"}|sonic' \
	'{"warp": "1", "skin": "amy", "unlockCharacters": true}|amy' '{"warp": "1", "skin": "nobody"}|sonic'; do
	got="$(skin "${v%%|*}")"
	[ "$got" = "skin ${v#*|}" ] || { ok=0; echo "  ${v%%|*}: want skin ${v#*|}, got '$got'"; }
done
case "$(cvars '{"warp": "1"}')" in *"cvar tutorialprompt Off "*) ;; *) ok=0; echo "  tutorialprompt is not Off by default" ;; esac
case "$(cvars '{"warp": "1", "tutorialPrompt": true}')" in *"cvar tutorialprompt On "*) ;; *) ok=0; echo "  tutorialPrompt true is not On" ;; esac
[ "$ok" = 1 ] && pass "settings: every other value of every setting is the engine's option (12 values), tutorialprompt, the unlocks (the game data's; every Record Attack map, the menu's), and the Start Map character (5 cases)" \
	|| bad "settings: a value did not reach the engine"
[ "$(cvars '{"warp": "1", "scoreTimeRings": "Classic"}')" != "$want" ] && pass "settings teeth: another value is not the default's" \
	|| bad "settings teeth: a value changed nothing - the leg cannot fail"

# ---- resolution: the picture's alone
content res '{"warp": "1"}'
states="" sounds="" pictures=""
for r in 320x200 1280x800 1920x1080 3840x2160; do
	printf '{"warp": "1", "resolution": "%s"}\n' "$r" > "$root/build/gate/res/settings"
	for build in nat box; do
		l="$($build res -n 260 -p 0 --input "$tests/gfz1-run.txt" | tail -1)"
		states="$states $(field "$l" state)"
		sounds="$sounds $(field "$l" audio)"
		[ "$build" = nat ] && pictures="$pictures $(field "$l" run)"
	done
done
one() { echo $1 | tr ' ' '\n' | sort -u | wc -l; }
if [ "$(one "$states")" = 1 ] && [ "$(one "$sounds")" = 1 ] && [ "$(one "$pictures")" = 4 ]; then
	pass "resolution: 320x200 to 3840x2160, native and sandboxed: one game state ($(echo $states | cut -d' ' -f1)), one sound, four pictures"
else
	bad "resolution: states '$states', sounds '$sounds', pictures '$pictures'"
fi
printf '{"warp": "1", "resolution": "1280x800"}\n' > "$root/build/gate/res/settings"
other="$(field "$(nat res -n 260 -p 0 --input "$root/build/gate/gfz1-nojump.txt" | tail -1)" state)"
[ "$other" != "$(echo $states | cut -d' ' -f1)" ] && pass "resolution teeth: another input is another game state" \
	|| bad "resolution teeth: the state digest did not see another input - the leg cannot fail"

# ---- opengl: the OpenGL renderer, sandboxed (the native reference has no GL)
content gl '{"warp": "1", "renderer": "opengl", "resolution": "320x200"}'
content glsoft '{"warp": "1", "resolution": "320x200"}'
gl_started="$("$wbxhost" "$core" "$root/build/gate/gl" -n 1 -p 0 2>&1 | grep -c '^OpenGL .*softpipe' || true)"
gl1="$(box gl -n 200 -p 10 --input "$tests/gfz1-run.txt" --ppm "$root/build/gate/gl.ppm")"
gl2="$(box gl -n 200 -p 10 --input "$tests/gfz1-run.txt")"
soft="$(box glsoft -n 200 -p 10 --input "$tests/gfz1-run.txt")"
lit="$(python3 -c '
import sys
d = open(sys.argv[1], "rb").read()
hdr = d.split(b"\n", 3); px = hdr[3]
print(sum(1 for i in range(0, len(px), 3) if px[i] + px[i + 1] + px[i + 2] > 30) * 100 // (len(px) // 3))' "$root/build/gate/gl.ppm" 2>/dev/null || echo 0)"
if [ "$gl_started" -ge 1 ] && [ -n "$gl1" ] && [ "$lit" -ge 50 ] && [ "$(field "$gl1" run)" != "$(field "$soft" run)" ]; then
	pass "opengl: SRB2's OpenGL renderer on the core's Mesa softpipe draws Greenflower ($lit% of the picture lit, not the software renderer's)"
else
	bad "opengl: OpenGL did not draw (started: $gl_started, lit: $lit%, run '$(field "$gl1" run)', software's '$(field "$soft" run)')"
fi
[ -n "$gl1" ] && [ "$gl1" = "$gl2" ] && pass "opengl: deterministic - two runs are the same run (every step's picture, the sound, the state)" \
	|| bad "opengl: two runs differ"
rr="$(box gl -n 200 -p 10 --input "$tests/gfz1-run.txt" --rerecord)"
ss="$(box gl -n 200 -p 10 --input "$tests/gfz1-run.txt" --session-at 30)"
[ "$gl1" = "$rr" ] && [ "$gl1" = "$ss" ] && pass "opengl: savestates - rerecord, and a new host at step 30 (mid-wipe), are the run without" \
	|| bad "opengl: a savestate changed the run (rerecord same: $([ "$gl1" = "$rr" ] && echo yes || echo no), session same: $([ "$gl1" = "$ss" ] && echo yes || echo no))"
stale="$(box gl -n 200 -p 10 --input "$tests/gfz1-run.txt" --stale-state 120)"
[ "$gl1" != "$stale" ] && pass "opengl teeth: a stale state (step 120 run twice) changes the run" \
	|| bad "opengl teeth: a stale state changed nothing - the leg cannot fail"
natgl="$("$native" "$root/build/gate/gl" -n 200 -p 10 --input "$tests/gfz1-run.txt" 2>&1)"
natsoft="$(nat glsoft -n 200 -p 10 --input "$tests/gfz1-run.txt")"
if echo "$natgl" | grep -q "OpenGL did not start; drawing in software" \
	&& [ "$(echo "$natgl" | grep -E '^(step|run) ')" = "$natsoft" ]; then
	pass "opengl: the native reference has no GL, says so, and draws in software"
else
	bad "opengl: the native reference did not fall back to software as it says"
fi

# ---- engine: the package through Chimera's libchimera
if [ -n "$bundle" ]; then
	sh "$here/build-package.sh" -m "${mb:-$MINIBOX_DIR}" -o "$root/build/gate/package" >/dev/null
	e="$(LD_LIBRARY_PATH="$bundle/dll" python3 "$here/tests/engine-open.py" "$bundle/dll/libchimera.so" \
		"$root/build/gate/package/srb2.chimeraCore" "$data" 450 2>/dev/null | grep "^450 steps")"
	nl="$(field "$(nat intro -n 450 -p 0 --input "$tests/menu-to-new-game.txt")" lag)"
	# which Chimera: a bundle's BUILD.txt, or a checkout's commit (its build/ is
	# the -c folder)
	which="$(head -2 "$bundle/BUILD.txt" 2>/dev/null | tail -1 | awk '{print $2}' | cut -c1-8)"
	[ -n "$which" ] || which="$(git -C "$bundle/.." rev-parse --short=8 HEAD 2>/dev/null || echo unknown)"
	case "$e" in
	"450 steps ($nl lag)"*) pass "engine: Chimera's engine ($which) opens the package and runs the menus into a new game: $e" ;;
	*) bad "engine: '$e' (run-native's lag: $nl)" ;;
	esac

	# opengl-hw: the renderer through Chimera's GPU bridge, where this machine
	# gives the engine a context (none: SKIP, it is the machine's, not the
	# core's); with no bridge asked for, opengl-hw is the Mesa, the same run
	pkg="$root/build/gate/package/srb2.chimeraCore"
	eo() { LD_LIBRARY_PATH="$bundle/dll" python3 "$here/tests/engine-open.py" "$bundle/dll/libchimera.so" "$pkg" "$data" "$@" 2>&1; }
	mesa="$(eo 120 '{"renderer": "opengl", "resolution": "320x200", "warp": "1"}' | grep '^120 steps')"
	nobridge="$(eo 120 '{"renderer": "opengl-hw", "resolution": "320x200", "warp": "1"}')"
	if echo "$nobridge" | grep -q "no GPU bridge (none offered); OpenGL on the Mesa softpipe" \
		&& [ -n "$mesa" ] && [ "$(echo "$nobridge" | grep '^120 steps')" = "$mesa" ]; then
		pass "engine: opengl-hw with no bridge offered says so and is the Mesa's run"
	else
		bad "engine: opengl-hw without a bridge is not the Mesa's run"
	fi
	gpu="$(eo 120 '{"renderer": "opengl-hw", "resolution": "320x200", "warp": "1"}' --gpu)"
	if echo "$gpu" | grep -q "OpenGL on the GPU outside the sandbox"; then
		driver="$(echo "$gpu" | grep -m1 '^chimera gl: ' | sed 's/^chimera gl: //')"
		hwsound="$(field "$(echo "$gpu" | grep '^120 steps' | tr -d ',')" sound)"
		mesasound="$(field "$(echo "$mesa" | tr -d ',')" sound)"
		states="$(LD_LIBRARY_PATH="$bundle/dll" python3 "$tests/engine-gpu-states.py" "$bundle/dll/libchimera.so" "$pkg" "$data" 2>/dev/null)"
		# the character select: flat fills drawn with the renderer's imageless
		# NOTEXTURE bound, which drew black on the GPU until gl_compat.c took an
		# imageless texture as texturing off (2026-10-05, from use)
		eo 300 '{"renderer": "opengl", "resolution": "320x200"}' --ppm "$root/build/gate/cs-mesa.ppm" >/dev/null
		eo 300 '{"renderer": "opengl-hw", "resolution": "320x200"}' --ppm "$root/build/gate/cs-gpu.ppm" --gpu >/dev/null
		apart="$(python3 -c '
import sys
a, b = (open(p, "rb").read().split(b"\n", 3)[3] for p in sys.argv[1:3])
n = len(a) // 3
print(sum(1 for i in range(0, len(a), 3) if abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2]) > 48) * 1000 // n)' \
			"$root/build/gate/cs-mesa.ppm" "$root/build/gate/cs-gpu.ppm" 2>/dev/null || echo 1000)"
		if [ "$apart" -le 10 ]; then
			pass "engine: opengl-hw's character select (flat fills, text, art) is the Mesa's picture ($apart per mille of pixels apart)"
		else
			bad "engine: opengl-hw's character select is not the Mesa's: $apart per mille of pixels apart"
		fi
		if [ -n "$hwsound" ] && [ "$hwsound" = "$mesasound" ] && echo "$states" | grep -q "^PASS rewind" \
			&& echo "$states" | grep -q "^PASS reopen"; then
			pass "engine: opengl-hw on the GPU ($driver): the Mesa's game (the same sound); a rewind and a reopen make the renderer again and draw the straight run ($(echo "$states" | grep '^straight' | sed 's/^straight: //'))"
		else
			bad "engine: opengl-hw on the GPU: sound $hwsound (the Mesa's $mesasound); $(echo "$states" | grep -E '^(PASS|FAIL)' | tr '\n' ' ')"
		fi
	else
		echo "SKIP engine: opengl-hw on a GPU - this machine gives the engine no GL context"
	fi
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
