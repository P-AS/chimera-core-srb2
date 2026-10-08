# chimera-core-srb2

[Sonic Robo Blast 2](https://srb2.org/) (Sonic Team Junior's 3D Sonic fangame, from Doom Legacy) as an
**unofficial** [Chimera](https://github.com/ToolAssisted-run/chimera) **game core** (`"kind": "game"`, see Chimera's
`docs/game-cores.md`). It is not maintained by the Chimera project and is not among the cores Chimera's README
lists; it needs no change to Chimera. See `docs/PLAN.md` for the milestones and decisions.

The engine is upstream SRB2 (`extern/SRB2`, master, net-compatible with 2.2.15), compiled from source with a
platform layer of the core's own (`waterbox/platform/`) in place of SDL: no window, no audio device (the core
mixes the sound itself, in the machine), no network, no threads. The game's data - srb2.pk3, zones.pk3,
characters.pk3, music.pk3 - is firmware: the core carries none of it.

## Using it in Chimera

Chimera ships no cores and downloads nothing. Download the core's `srb2.chimeraCore` package from this repository's
[Releases](https://github.com/P-AS/chimera-core-srb2/releases) page (a rolling `dev` build, dated
`nightly-YYYY-MM-DD` builds), or build it, and put it in the `Cores` folder beside `Chimera.exe` (or the folder
chosen in File > Core Manager > Change folder...). The same file works on Linux and on Windows. None of the game's
data is in the package: a project brings SRB2 2.2.15's srb2.pk3, zones.pk3, characters.pk3 and music.pk3 as
firmware.

## Building

```
git submodule update --init
export MINIBOX_DIR=<a Chimera checkout>/extern/chimera-common-minibox
waterbox/setup-mesa.sh                      # Mesa's softpipe for the guest, once (~15 min): build/mesa/
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses: build/native/
make -C waterbox -f guest.mk -j$(nproc)     # the core: build/guest/core.wbx
```

miniBox must be built first, with its C++ guest toolchain (GME is C++), in its checkout:

```
meson setup build/meson-cpp -Dguest_cpp=true && ninja -C build/meson-cpp
ninja -C build/meson-cpp libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
``` 

`setup-mesa.sh` fetches Mesa 24.0.9 (checked by SHA256) and builds its softpipe behind OSMesa for the guest:
the OpenGL renderer draws on it, inside the sandbox. It needs meson, python3-mako, bison and flex; both builds
take Mesa's GL headers from it.

The patches go onto
`extern/SRB2` and `extern/openmpt` on the first build (`waterbox/apply-patches.sh [openmpt]`, each series all or
nothing). `extern/openmpt` is shallow: `git submodule update --init` fetches only its pinned commit.

## Data for the gate

`waterbox/fetch-data.sh [<folder>]` downloads STJr's own SRB2 2.2.15 release and takes out the four pk3s, checked
against the declared firmware (default `build/srb2-2.2.15`); `run-gate.sh -d <folder>` runs on them. CI
(`.github/workflows/chimera.yml`) does this, then the gate, the package and Chimera's contract tests, and publishes
`dev` and nightly releases.

## The package

```
waterbox/build-package.sh [-r <Chimera bundle or checkout>]   # build/package/srb2.chimeraCore, or into its Cores
```

`-r <chimera>` writes the package into a Chimera source checkout's `build/Cores` (a bundle's `Cores`). The whole
build, as CI does it, is in [docs/BUILDING.md](docs/BUILDING.md); [AGENTS.md](AGENTS.md) is the guide for an AI
coding agent.

## Running the native reference

```
mkdir -p build/work && ln -s /usr/share/games/SRB2/*.pk3 build/work/
echo '{"warp": "1"}' > build/work/settings          # optional: start in Greenflower Zone Act 1
build/native/run-native build/work -n 400 --ppm frame.ppm
build/native/run-wbx build/guest/core.wbx build/work -n 400   # the same, in the sandbox
```

Both run `-n` steps (a step is a tic, on the machine's own clock), print the picture's hash, the tic and the
clock every `-p` steps and a hash of the whole run at the end, and `--ppm` writes the last picture. Their
lines diff directly.

## The gate

`./waterbox/run-gate.sh [-d <SRB2 data folder>] [-m <miniBox>]`: the legs, each saying what it compared, each
with teeth. `equivalence`: native == sandbox, step for step, on the intro and Greenflower Zone Act 1. `steps`:
a step is a tic, a wipe's frames are lag steps, play is a tic a step. `savestates`: a save and load before
every step, and a new host mid-wipe, change nothing. `declaration`: the package's controller is the core's. `settings`: each setting reaches the engine.
`opengl`: the OpenGL renderer draws, deterministically, and savestates change nothing. `engine` (with `-c`) also
runs `opengl-hw` through the GPU bridge where the machine has a context: the same game as the Mesa, and the
picture right again after a rewind and a reopen.
`properties`: the Game State domain, read by its property table, holds what Greenflower's movie did, the timers
`tests/timers.lua` sets, and a spindash's charge (`tests/spindash.txt`).
`input`: movies through the menus and in Greenflower,
the same every way. `files`: what the game writes is kept in the machine and exported identically. `time`: a
host stall mid-run changes nothing. `audio`: the music and sounds are heard, and every leg's run line carries
the sound's hash too.

The harnesses' `--wav FILE` writes a run's sound.

## Settings

Declared in `waterbox/waterbox.config`, so Chimera shows them as the core's options; each is SRB2's own option,
given to the engine at start: Play Style (default Manual), Camera Speed (1.0), Camera Distance (192), Camera Height (40), Score/Time/Rings (Mania), Flip
Camera with Gravity (Yes), Automatic Braking (On), Tutorial Prompt (Off), Unlock Record Attack/NiGHTS Mode/Marathon Run, Unlock All
Characters, Unlock All Maps in Record Attack and NiGHTS Mode, Unlock All Secrets (each Off), Resolution (1280x800: SRB2's video modes, and 2560x1440 and 3840x2160, shown with square
pixels; the game plays the same at every one), Renderer (software; opengl: SRB2's OpenGL renderer on the
Mesa softpipe the core carries - deterministic and savestate-safe, but slow: about 15 fps at 320x200, under 1
at 1280x800 with shaders; opengl-hw: the same renderer on this machine's GPU through Chimera's GPU bridge -
fast, its picture the driver's, the same game - and on the Mesa where there is no bridge; the renderer is part
of the game, so a movie is its renderer's), OpenGL Shaders (On; the picture's alone), Start Map (empty: the intro and the title), Start Map Character (a skin name, e.g. knuckles; empty: Sonic).

## The controller

SRB2's default keyboard as buttons (Forward, Jump, Spin... the menus' Enter and Escape, which answer a prompt too),
pressed as keys, so menus and play work as on a keyboard (the ring-slinger controls are left out for now); plus axes for exact values: Forward Move, Side Move, Turn, Aim. The
harnesses take a movie as text, `--input FILE` with `FROM-TO: Button; Axis=value` lines (`waterbox/tests/`).

## Game State and the TAS info script

The core exposes one memory domain, `Game State`, and a property table naming its fields, so RAM Watch (Watches >
Add Game Properties), RAM Search and Lua's `game.get` see them by name. The core copies them from the game after
every step; they are read-only. `Game.Tic`, `Game.Level Time`, `Game.State`, `Game.Map`; `Player.In Level`,
`Player.X`/`Y`/`Z`, `Player.Momentum X`/`Y`/`Z`, `Player.Conveyor Momentum X`/`Y` (`player->cmomx`/`cmomy`: what a
conveyor or a moving platform adds), `Player.Platform Momentum Z` (`mo->pmomz`: the moving floor's) and
`Player.Speed` (16.16 fixed point: 65536 is one unit; speed is the game's own `player->speed`), `Player.Dash Speed`,
`Player.Min Dash`, `Player.Max Dash` (the spindash's charge and its bounds, 16.16), `Player.Flags` (`pflags`) and
`Player.Charging Spindash` (its `PF_STARTDASH` bit), `Player.Angle` (2^32 a turn); `Timers.Speed Shoes`, `Timers.Invincibility`,
`Timers.Space`, `Timers.Air` (tics left). Out of a level the player's fields are 0.

`lua/tasinfo.lua` draws them in the bottom right corner of the game, clear of Chimera's own HUD: open it in Tools > Lua Console with an SRB2 project loaded. It shows
speed, the angle in hex and in degrees (each to 4 decimal places, on lines of their own), the position and the
momentum, the conveyor and platform momentum, the spindash's revs, and the four timers in tics and seconds. SRB2
has no discrete revs: a spindash charges 1.0 of speed a tic while Spin is held, from Min Dash to Max Dash (Sonic: 15
to 70), so the script counts a rev a tic, as revs so far / revs to full charge. The conveyor and platform momentum,
the revs and the timers are shown only while they are not 0.

The harnesses' `--game-state FILE` writes every step's block, and the table beside it in `FILE.json`.

## Licence

GPL-2.0, as SRB2 is (`LICENSE`). zlib and libpng are compiled from the copies in SRB2's `libs/`, under their
own licences; libco (miniBox's `extern/libco`) is public domain; libogg and libvorbis (`extern/ogg`,
`extern/vorbis`) are BSD-3-Clause; Game_Music_Emu (`extern/gme`) is LGPL-2.1-or-later (its Nuked YM2612 too); libopenmpt (`extern/openmpt`) is
BSD-3-Clause. Sonic the Hedgehog and related characters are trademarks of SEGA; this repository carries
none of the game's data.
