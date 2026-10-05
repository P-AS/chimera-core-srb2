# chimera-core-srb2

[Sonic Robo Blast 2](https://srb2.org/) (Sonic Team Junior's 3D Sonic fangame, from Doom Legacy) as an
**unofficial** [Chimera](https://github.com/ToolAssisted-run/chimera) **game core** (`"kind": "game"`, see Chimera's
`docs/game-cores.md`). It is not part of Chimera's official roster and is not maintained by the Chimera project:
install it by hand (File > Core Manager lists it as added by hand) from this repository's releases
(github.com/P-AS/chimera-core-srb2). See `docs/PLAN.md` for the milestones and decisions.

The engine is upstream SRB2 (`extern/SRB2`, master, net-compatible with 2.2.15), compiled from source with a
platform layer of the core's own (`waterbox/platform/`) in place of SDL: no window, no audio device (the core
mixes the sound itself, in the machine), no network, no threads. The game's data - srb2.pk3, zones.pk3,
characters.pk3, music.pk3 - is firmware: the core carries none of it.

## Building

```
git submodule update --init
export MINIBOX_DIR=<a Chimera checkout>/extern/chimera-common-minibox
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses: build/native/
make -C waterbox -f guest.mk -j$(nproc)     # the core: build/guest/core.wbx
```

miniBox must be built first, with its C++ guest toolchain (GME is C++), in its checkout:

```
meson setup build/meson-cpp -Dguest_cpp=true && ninja -C build/meson-cpp
ninja -C build/meson-cpp libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
``` 

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
`input`: movies through the menus and in Greenflower,
the same every way. `files`: what the game writes is kept in the machine and exported identically. `time`: a
host stall mid-run changes nothing. `audio`: the music and sounds are heard, and every leg's run line carries
the sound's hash too.

The harnesses' `--wav FILE` writes a run's sound.

## Settings

Declared in `waterbox/waterbox.config`, so Chimera shows them as the core's options; each is SRB2's own option,
given to the engine at start: Play Style (default Manual), Camera Speed (1.0), Score/Time/Rings (Mania), Flip
Camera with Gravity (Yes), Automatic Braking (On), Tutorial Prompt (Off), Unlock Record Attack/NiGHTS Mode/Marathon Run, Unlock All
Characters, Unlock All Secrets (each Off), Resolution (1280x800: SRB2's video modes, and 2560x1440 and 3840x2160, shown with square
pixels; the game plays the same at every one), Start Map (empty: the intro and the title).

## The controller

SRB2's default keyboard as buttons (Forward, Jump, Spin... the menus' Enter and Escape, a prompt's Yes and No),
pressed as keys, so menus and play work as on a keyboard (the ring-slinger controls are left out for now); plus axes for exact values: Forward Move, Side Move, Turn, Aim. The
harnesses take a movie as text, `--input FILE` with `FROM-TO: Button; Axis=value` lines (`waterbox/tests/`).

## Licence

GPL-2.0, as SRB2 is (`LICENSE`). zlib and libpng are compiled from the copies in SRB2's `libs/`, under their
own licences; libco (miniBox's `extern/libco`) is public domain; libogg and libvorbis (`extern/ogg`,
`extern/vorbis`) are BSD-3-Clause; Game_Music_Emu (`extern/gme`) is LGPL-2.1-or-later (its Nuked YM2612 too); libopenmpt (`extern/openmpt`) is
BSD-3-Clause. Sonic the Hedgehog and related characters are trademarks of SEGA; this repository carries
none of the game's data.
