# chimera-core-srb2

[Sonic Robo Blast 2](https://srb2.org/) (Sonic Team Junior's 3D Sonic fangame, from Doom Legacy) as a
[Chimera](https://github.com/ToolAssisted-run/chimera) **game core** (`"kind": "game"`, see Chimera's
`docs/game-cores.md`). Work in progress: see `docs/PLAN.md` for the milestones and decisions.

The engine is upstream SRB2 (`extern/SRB2`, master, net-compatible with 2.2.15), compiled from source with a
platform layer of the core's own (`waterbox/platform/`) in place of SDL: no window, no audio device, no
network, no threads. The game's data - srb2.pk3, zones.pk3, characters.pk3, music.pk3 - is firmware: the
core carries none of it.

## Building

```
git submodule update --init
make -C waterbox -f native.mk -j$(nproc)    # the native reference: build/native/run-native
```

The patches go onto `extern/SRB2` on the first build (`waterbox/apply-patches.sh`, all or nothing).

## Running the native reference

```
mkdir -p build/work && ln -s /usr/share/games/SRB2/*.pk3 build/work/
build/native/run-native build/work -n 400 --ppm frame.ppm
```

It runs the engine's loop `-n` times, printing a hash of the picture every `-p` frames, and writes the last
picture with `--ppm`. A step is a tic, on the machine's own clock.

## The gate

`./waterbox/run-gate.sh [-d <SRB2 data folder>]`: the legs, each saying what it compared. So far `time`: a
host stall mid-run changes nothing, and on the host's clock it would (the leg's teeth).

## Licence

GPL-2.0, as SRB2 is (`LICENSE`). zlib and libpng are compiled from the copies in SRB2's `libs/`, under their
own licences. Sonic the Hedgehog and related characters are trademarks of SEGA; this repository carries
none of the game's data.
