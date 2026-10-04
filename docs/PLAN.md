# chimera-core-srb2: plan and decisions

Started 2026-10-04, from the DSDA core's shape (`waterbox/`, the patch series, the native reference beside
the guest build), following Chimera's `docs/porting-a-core.md` and `docs/game-cores.md`.

## Milestones

- [x] **0. Headless native build.** SRB2's engine with the core's platform layer in place of `sdl/`: no
  window, no SDL, no audio device, no network, no threads. `build/native/run-native` boots 2.2.15's data and
  runs the intro in the software renderer.
- [x] **1. Virtual time.** The machine's clock instead of the host's (`platform/i_system.c`): a step is a
  tic, sleeps jump to the next tic's deadline, the libc clocks and `rand` are wrapped. Gate leg `time`: a
  300 ms host stall mid-run changes nothing; on `--host-clock` the same stall changes the run (teeth).
- [x] **2. Guest build.** `core.wbx` through miniBox's C guest toolchain (`MINIBOX_DIR`; plain C, no
  libstdc++), `check-wbx` clean. The exports (`wbx-entry.c`) are the same code in both builds: run-native calls
  them directly, run-wbx through miniBox's host, over one harness loop (`harness.h`). Gate leg `equivalence`:
  native == sandbox on the intro (700 steps) and Greenflower Zone Act 1 (`warp` 1, 1000 steps), every step's
  picture, tic and clock; teeth: native on the host's clock is not the sandbox. `time` runs in both builds.
- [ ] 3. The machine's filesystem: SRB2's folder is already the machine's root (`-workdir .`, see "The game's
  home"); what it holds (config from the settings, game data as a project file, saves in memory) is next.
  Until then nothing is written, in either build (`platform/files.c`).
- [ ] 4. Input (the base tic command, `I_BaseTiccmd`), lag. **Lag is done**: wipes are steps (below); the
  controller and the tic command from it are next.
- [ ] 5. Audio: a mixer of the core's own for the effects and music (music.pk3 is OGG/tracker/MIDI).
- [ ] 6. Savestates: **rerecord and session pass** (gate leg `savestates`, ~32 MB a state), including a state
  taken mid-wipe with the engine suspended on its cothread. 7. the package, properties (`Game State`),
  settings.

## Upstream

- **The submodule is SRB2 master** (`extern/SRB2`, 639b58c6d, 2.2.15-113), not the 2.2.15 tag: it is
  net-compatible with 2.2.15 and carries the fixes that let it build with GCC 15 and later (user-decided,
  2026-10-04). **2.2.16 is due soon**, so everything is built to be bumped:
  - the sources are upstream's own `Sourcefile` lists (`waterbox/sources.mk`), so new and removed files follow
    the submodule;
  - zlib and libpng are the copies in upstream's `libs/`, not submodules of the core's;
  - the patches are hooks, small and few. To bump: move the submodule, `waterbox/apply-patches.sh` (it says
    which patch no longer applies), rebuild, and update the firmware hashes when the data changes
    (`ASSET_HASH_*` in upstream's `src/config.h.in`, which the engine checks itself at start).
- **The data is firmware, never carried**: srb2.pk3, zones.pk3, characters.pk3, music.pk3 (2.2.15's;
  `/usr/share/games/SRB2` locally). The engine refuses srb2/zones/characters.pk3 whose MD5 is not 2.2.15's
  (`W_VerifyFileMD5`; `DEVELOP` is not defined).

## The patches

- `0001-loop-for-the-host.patch`: `D_SRB2Loop` in two parts, `D_SRB2LoopSetup()` and `D_RunFrame()`, for a host
  that runs the loop itself.
- `0002-no-curl.patch`: `d_netfil.c`'s HTTP download under `HAVE_CURL`, as its include already is (upstream's
  CMake makes curl mandatory, so nothing else guards it); without it, no download.
- `0003-workdir-backport.patch`: upstream's `-workdir` (da1b35820, on `next` for 2.2.16), backported: it names
  SRB2's own folder, where `-home` names only the user's home it is otherwise derived from (`<home>/.srb2`).
  **Drop it when the submodule reaches 2.2.16**: `apply-patches.sh` will report it no longer applies.

## The platform layer (`waterbox/platform/`)

- `i_system.c`: upstream's system layer without a host. `I_Error`/`I_Quit` halt the machine (the driver
  longjmps out of the engine; the machine stays, still). `I_LocateWad` is "." (where the host mounts the
  files). `I_GetEnv` answers nothing, so the home is `-home .`.
- `i_video.c`: the software renderer's buffer, one mode (320x200), the palette the engine last set; the
  overlays upstream's `I_FinishUpdate` draws (ticrate, captions, marathon timer) are kept: they are the
  picture's.
- `i_threads.c`: one thread (`I_can_thread` is false); `i_net.c`: no network.
- `comptime.c`: the build's date and git state fixed, so both builds draw the same console.
- From upstream as they are: `dummy/i_sound.c` (silence, for now), `dummy/i_net.c`, `dummy/i_cdmus.c`,
  `sdl/dosstr.c`.

## The guest (milestone 2)

- **The files are the mounts, by name**: miniBox finds a file by its exact name, and the engine opens
  `./srb2.pk3` (its WAD folder is "."). `platform/files.c` wraps `fopen`, `access`, `stat` and `remove` in both
  builds: a leading `./` goes, nothing is written (a write-mode `fopen` fails, `remove` does nothing), and
  `access` is answered by opening (miniBox has none). The harnesses write their pictures with `open`/`write`,
  which the link does not wrap.
- **The settings** are miniBox's `settings` JSON (`waterbox_settings.h`), read in `Init` in both builds. So far
  `warp`: a map to start in (`-warp`); empty, the game's own start.
- **Memory**: `run-wbx`'s layout is sbrk 256, sealed 4, invisible 32, plain 4, mmap 1024 MiB (the picture and
  the audio buffer are invisible: output, not the machine). Not yet measured against a whole game.
- **miniBox's guest toolchain is not in its default target**: `ninja -C <miniBox>/build/meson-linux
  source/guest/emulibc.c.o` builds musl-gcc, the sysroot and emulibc (`guest.mk` says so when they are missing).
- Not yet seen: a libm difference between glibc and musl (the DSDA core wraps five functions for its
  renderer). SRB2's game and renderer are fixed-point; the gate's contents agree. A longer run, other zones
  and the special stages will say more.

## Wipes are steps (2026-10-04)

SRB2 draws its wipes in loops that run a frame a tic without running the game: the fade to and from black on
every game-state change (`D_Display`), the fade before a level loads and the special stage's white
(`P_LoadLevel`, inside `G_Ticker`), the level's title card (`G_PreLevelTitleCard`, 24 tics), the intro's and the
custom cutscenes' (`f_finale.c`). Each waits for the next tic with `I_Sleep`. At milestone 1 a sleep moved the
clock, so a whole wipe passed inside one step: invisible, and a movie shorter than play by every wipe.

Now **the engine runs on a cothread of its own** (libco, miniBox's `extern/libco`, public domain;
`srb2-driver.c`). A step moves the clock one tic and resumes it, and `I_Sleep` hands back to the host: **each
wipe frame is a step**, with its own picture. **A step that built no tic command is lag** (`InputWasRead`: set
when `G_BuildTiccmd` asks for the base command, `I_BaseTiccmd`). No game logic runs in a wipe, so nothing about
sync changes; the movie is as long as play. No upstream patch: it covers every wait loop, an add-on's cutscenes
and 2.2.16's too. An exit (`I_Error`) leaves the cothread for good instead of longjmp'ing.

Entering Greenflower Zone Act 1 is 64 lag steps (fade, title card, fade in); the intro is 193 of its first
700.

**After a wipe, a step can run up to three tics**: SRB2 calls `NetUpdate` three times a pass (`TryRunTics`,
`R_RenderView`, the end of `D_Display`), and each makes a tic for the time passed since the last. A pass that
spans a wipe makes several, and the next pass runs them all: what SRB2 does on any machine, the wipe having
taken the time. Each of those tics' commands is built in a step (in the `NetUpdate` that made it, from that
step's input), so the movie still says every tic; it is only not a tic a row for those few steps. In play every
step is one tic (the gate checks).

## The game's home

**SRB2's folder is the machine's root, never a folder of the host's.** `Init` starts the engine with
`-workdir .` (patch 0003) and `-home .`. Everything SRB2 keeps is built from that folder (`srb2home`), so its
files are the machine's own top-level names: `config.cfg`, `gamedata.dat` (unlocks, records), `srb2sav*.ssg`
(saves), `autoexec.cfg`, `replay/<folder>/MAPxx-*.lmp` (record attack), `luafiles/`, `addons/`. Without
`-workdir`, the folder would be `./.srb2/`, a subfolder the flat, name-matched mounts of miniBox would have to
spell. `-home` is still required (SRB2 stops without a user home) and is otherwise unused.

Nothing can reach the host's `~/.srb2` in either build. The core's `I_GetEnv` answers no `$HOME`, and paths
are relative to the machine's root: the mounts in the sandbox, the harness's work folder natively. Writes are
refused (`platform/files.c`) and `I_mkdir` makes nothing. Verified: a `config.cfg` at the work folder's root
is executed (`Executing ./config.cfg`) and changes the picture identically in both builds.

**Milestone 3** decides what that folder holds in Chimera's terms. The likely shape:
- `config.cfg` is not the user's: settings that shape the machine come from the project. The core writes its
  own config from the settings, or none, so a host's SRB2 configuration can never leak in.
- `gamedata.dat` is a project file (unlocked characters, emblems and levels change what the game offers). It
  is mounted read-only from the project or starts empty, and written back as save data
  (Chimera's `docs/save-data.md`).
- Saves and replays are written into the machine's memory, so they are part of the savestate. That is
  porting-a-core.md's "serve the machine's drives out of its own memory".

## What the engine asks of the host, and what the core must answer

Found reading the engine, for the milestones ahead:

- **The game's RNG is seeded from the OS at start**: `D_SRB2Main` seeds `M_Random` from `I_GetRandomBytes` and
  `P_SetRandSeed(M_RandomizedSeed())`. The core's `I_GetRandomBytes` is a splitmix64 stream of the driver's
  `chimera_random_seed` - a setting later, as DSDA's `-rngseed`.
- **Time** (done, milestone 1): `I_UpdateTime` turns `I_GetPreciseTime` deltas into tics with a double accumulator and a strict
  `>` - a clock that moves exactly one tic per step yields no tic on the first (1/35 is not more than 1/35)
  and one per step after. The clock starts half a tic in and stays on half-tics, clear of the threshold.
- **Loops wait inside a tic** on `I_Sleep` + `I_UpdateTime`: the wipes, the title card, the intro: each frame
  is a step (above, "Wipes are steps").
- `time()`, `clock()`, `localtime()` in Lua's `os` library (`loslib.c`) and `d_netfil.c`; `rand()` in
  `d_netfil.c` and `d_net.c`'s packet drop: wrapped (`WRAP_FLAGS`): the machine's time since 2000-01-01 UTC,
  `localtime` as UTC, and a `rand` of the core's own (glibc's and musl's differ).
- The frame-rate cap sleeps (`I_SleepDuration`) and interpolation (`cv_fpscap`, `rendertimefrac`) read the
  precise clock: interpolation must be off (one picture per tic).
- The configuration (`config.cfg`) and game data (`gamedata.dat`) live in SRB2's folder, the machine's root
  (above, "The game's home").
