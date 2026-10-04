# chimera-core-srb2

[Sonic Robo Blast 2](https://srb2.org/) (Sonic Team Junior's 3D Sonic fangame, from Doom Legacy) as a
[Chimera](https://github.com/ToolAssisted-run/chimera) **game core** (`"kind": "game"`, see Chimera's
`docs/game-cores.md`). Work in progress: see `docs/PLAN.md` for the milestones and decisions.

The engine is upstream SRB2 (`extern/SRB2`, master, net-compatible with 2.2.15), compiled from source with a
platform layer of the core's own (`waterbox/platform/`) in place of SDL: no window, no audio device (the core
mixes the sound itself, in the machine), no network, no threads. The game's data - srb2.pk3, zones.pk3, characters.pk3, music.pk3 - is firmware: the
core carries none of it.

## Building

```
git submodule update --init
export MINIBOX_DIR=<a Chimera checkout>/extern/chimera-common-minibox
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses: build/native/
make -C waterbox -f guest.mk -j$(nproc)     # the core: build/guest/core.wbx
```

miniBox must be built first, with its guest toolchain: `meson setup build/meson-linux && ninja -C
build/meson-linux && ninja -C build/meson-linux source/guest/emulibc.c.o` in its checkout. The patches go onto
`extern/SRB2` on the first build (`waterbox/apply-patches.sh`, all or nothing).

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
every step, and a new host mid-wipe, change nothing. `input`: movies through the menus and in Greenflower,
the same every way. `files`: what the game writes is kept in the machine and exported identically. `time`: a
host stall mid-run changes nothing. `audio`: the music and sounds are heard, and every leg's run line carries
the sound's hash too.

The harnesses' `--wav FILE` writes a run's sound.

## The controller

SRB2's default keyboard as buttons (Forward, Jump, Spin... and the menus' Enter and Escape), pressed as keys, so
menus and play work as on a keyboard; plus axes for exact values: Forward Move, Side Move, Turn, Aim. The
harnesses take a movie as text, `--input FILE` with `FROM-TO: Button; Axis=value` lines (`waterbox/tests/`).

## Licence

GPL-2.0, as SRB2 is (`LICENSE`). zlib and libpng are compiled from the copies in SRB2's `libs/`, under their
own licences; libco (miniBox's `extern/libco`) is public domain; libogg and libvorbis (`extern/ogg`,
`extern/vorbis`) are BSD-3-Clause. Sonic the Hedgehog and related characters are trademarks of SEGA; this repository carries
none of the game's data.
