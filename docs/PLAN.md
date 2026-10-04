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
- [ ] **2. Guest build.** `core.wbx` through miniBox's musl toolchain (`MINIBOX_DIR`), native == sandbox.
- [ ] 3. The machine's filesystem (config, game data, saves inside the machine).
- [ ] 4. Input (the base tic command, `I_BaseTiccmd`), lag.
- [ ] 5. Audio: a mixer of the core's own for the effects and music (music.pk3 is OGG/tracker/MIDI).
- [ ] 6. Savestates, 7. the package, properties (`Game State`), settings.

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

## What the engine asks of the host, and what the core must answer

Found reading the engine, for the milestones ahead:

- **The game's RNG is seeded from the OS at start**: `D_SRB2Main` seeds `M_Random` from `I_GetRandomBytes` and
  `P_SetRandSeed(M_RandomizedSeed())`. The core's `I_GetRandomBytes` is a splitmix64 stream of the driver's
  `chimera_random_seed` - a setting later, as DSDA's `-rngseed`.
- **Time** (done, milestone 1): `I_UpdateTime` turns `I_GetPreciseTime` deltas into tics with a double accumulator and a strict
  `>` - a clock that moves exactly one tic per step yields no tic on the first (1/35 is not more than 1/35)
  and one per step after. The clock starts half a tic in and stays on half-tics, clear of the threshold.
- **Four loops wait inside a tic** on `I_Sleep` + `I_UpdateTime`: the wipe (`f_wipe.c`), the level load's
  fade (`p_setup.c`), the intro/finale (`f_finale.c`), `g_game.c`'s. `I_Sleep` moves the clock to the next
  tic's deadline, so each ends; **its tics pass inside one step** (the intro's wipes: 704 tics in 700
  steps). Whether a wipe should instead be steps of its own, lag frames as DSDA's stepped melt is, is
  milestone 4's question (input and lag).
- `time()`, `clock()`, `localtime()` in Lua's `os` library (`loslib.c`) and `d_netfil.c`; `rand()` in
  `d_netfil.c` and `d_net.c`'s packet drop: wrapped (`WRAP_FLAGS`): the machine's time since 2000-01-01 UTC,
  `localtime` as UTC, and a `rand` of the core's own (glibc's and musl's differ).
- The frame-rate cap sleeps (`I_SleepDuration`) and interpolation (`cv_fpscap`, `rendertimefrac`) read the
  precise clock: interpolation must be off (one picture per tic).
- The configuration (`config.cfg`) and game data (`gamedata.dat`) are written under `-home`: inside the
  machine at milestone 3.
