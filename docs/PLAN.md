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
- [x] **3. The machine's filesystem** (`platform/files.c`): SRB2's folder is the machine's root (`-workdir .`);
  what the game writes is kept in the machine's memory, shadowing the read-only mounts; the files are the
  save data export (`GetSaveData*`). Gate leg `files`. Left for the package: the `savedata` slot to take an
  export back in (see "The machine's filesystem").
- [x] **4. Input** (`srb2-input.c`): SRB2's keyboard as buttons (key events: the menus work) plus axes into the
  tic command (`I_BaseTiccmd`) for exact values; lag = no tic command built. Gate leg `input`: a movie through
  the menus into the Tutorial Zone, and one running in Greenflower.
- [ ] **5. Audio** (user-decided: a C mixer of the core's own; libvorbis, not stb_vorbis; GME and libopenmpt -
  libxmp if libopenmpt proves impractical; no MIDI for now):
  - [x] **5a.** The mixer (`platform/i_sound.c`): sound effects (DMX, WAV, Ogg Vorbis), Ogg Vorbis and WAV
    music with loop points and fades. Gate leg `audio`; every leg's run line carries the sound's hash.
  - [x] **5b.** GME (libgme 0.6.5: VGM/VGZ, NSF, SPC, GBS, HES, KSS, AY, SAP, GYM), music and sound effects,
    as upstream's mixer drives it. C++: the guest is built with miniBox's C++ toolchain.
  - [ ] 5c. libopenmpt (tracker modules; libxmp if libopenmpt is impractical): C++; its SIMD chosen by the
    host's CPU at run time must be compiled out.
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
- `0004-driver-aiming.patch`: a non-zero aiming in the base tic command (`I_BaseTiccmd`, the external driver's)
  sets the look pitch; `G_BuildTiccmd` otherwise overwrites it with its own look state.
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

## The controller (2026-10-04, user-decided: keys + analog axes)

A movie row is **SRB2's own keyboard plus four axes**:

- **The buttons are the game's controls in its default keyboard scheme** ("FPS", `gamecontroldefault[gcs_fps]`):
  Forward (W), Backward (S), Strafe Left/Right (A/D), Turn Left/Right (←/→), Look Up/Down (↑/↓), Jump
  (Space), Spin (Left Shift), Fire (Right Ctrl), Fire Normal (Right Alt), Toss Flag ('), Center View (Left Ctrl),
  Camera Reset (R), Camera Toggle (V), Weapon Next/Prev (the wheel), Weapon 1-7, Custom 1-3 (Z/X/C), Pause (P),
  and the menus' Enter and Escape. A button that changes is a key event (`D_PostEvent`), so the menus, the
  title, a prompt and the game read it as a keyboard, and the game builds its tic command with all its own
  logic (accelerative turning, the Simple style's camera, Lua's `PlayerCmd`). In menus the arrows are Look
  Up/Down and Turn Left/Right, as on a keyboard; a yes/no prompt takes Enter as yes and Escape as no.
- **The bindings are forced to that scheme after the start** (`srb2_input_bind`): a configuration or an
  `autoexec.cfg` cannot change what a recorded button means.
- **The axes** go in through upstream's seam for an external driver, `I_BaseTiccmd` ("empty, or external
  driver"), the command `G_BuildTiccmd` starts from: Forward Move and Side Move (-50..50), Turn (an angle delta
  in 1/65536 turns, added to the turn keys'), Aim (the look pitch; 0 leaves the game's own look). The game adds
  its keys' movement to the base without clamping the sum, so a movement axis counts only while its keys are
  not held. `G_BuildTiccmd` overwrites the command's aiming with its own look state (which springs back to
  level), so **patch 0004** lets a non-zero base aiming set the pitch.
- `FrameAdvance`'s mask is the first 64 buttons; `SetButton` and `SetAxis` the rest; `GetButtonName`/
  `GetAxisName` name them, in the controller's order (for the declaration, and the harnesses' `--input`).
- Not yet: player 2 (splitscreen), text entry (a name, the console), the joystick-style analog
  configuration. The control style, the camera and the other options that shape the tic command are SRB2's
  defaults until they are settings (milestone 7).

The harnesses take a movie as text (`--input FILE`: `FROM-TO: Button; Axis=value` a line);
`waterbox/tests/` holds the gate's two.

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

## The sound (milestone 5)

**The mixer is the core's own and runs in the machine** (`platform/i_sound.c`, in place of upstream's
`dummy/i_sound.c`): after each step it renders that step's 1260 frames (44.1 kHz stereo, 16-bit; 44100/35) into
the buffer `GetAudio` returns. **Game logic reads the sound's state**, which is why it must be the machine's:
a boss waits for its death sound (`p_enemy.c`, `S_SoundPlaying`), the change-music linedef seeks relative to
the song's position and loop point (`p_spec.c`), the music stack keeps positions for jingles, and Lua has
`S_SoundPlaying`, `S_IdPlaying`, `S_MusicPlaying`, `S_GetMusicPosition`, `S_GetMusicLength`. A movie through the
menus into the Tutorial Zone drew different pictures once there was sound: silence would have made a movie's
sync depend on whether the host could play sound.

It does what upstream's SDL_mixer backend (`sdl/mixer_sound.c`) does:

- **Sound effects**: DMX (upstream's `ds2chunk`, ported), PCM WAV and Ogg Vorbis, converted once at load to
  44.1 kHz stereo (linear, 16.16 fixed point); volume and panning a channel as `Mix_Volume`/`Mix_SetPanning`
  give them; **pitch ignored**, as SDL_mixer ignores it. 256 channels, the game's channel numbers.
- **Music**: Ogg Vorbis (libvorbis) and WAV; the loop point from the song's `LOOPPOINT=` (samples, with
  upstream's own `(44.1 + n) / 44100`) or `LOOPMS=` tag, or the game's (`I_SetSongLoopPoint`); at the end, a
  looping song seeks to it, another stops (upstream's `music_loop`, its fade-timing hack included). Volume as
  `get_real_volume`: the volume to a 128 scale, times the fade's percentage.
- **Fades** step every 10 ms of output (441 frames), as upstream's SDL timer steps them, with the callback
  deferred to `I_UpdateSound`. Rounding to 10 ms, the source and target logic: upstream's.
- **Length and position** are the decoder's (as SDL_mixer_X, upstream's Windows builds, gives them; plain
  SDL_mixer answers 0 for the length here, as this code never reads `LENGTHMS=`). No tempo for a stream
  (`I_SetSongSpeed` false), as SDL_mixer has none.
- **MIDI is not played** (user-decided, for now): the game prefers the digital songs (`O_` lumps), which the
  base data has for every MIDI one (`D_`).

Checked against independent decodes (ffmpeg): the intro's song is in the mix with a correlation of 1.0000
(residual 0.04%: the volume's integer rounding), at the volume the game sets (16 of 31 → 66/128); the jump
sound starts on the step Jump was pressed.

**GME** (`extern/gme`, libgme 0.6.5, every emulator, the Nuked YM2612, its CMake defaults) does what upstream's
mixer does with it: tried first for a song (a VGZ inflated by SRB2's own code, `inflate_vgz`), then Vorbis; a
sound effect rendered for its `play_length`; the equalizer at treble 5, bass 1; looping by
`gme_set_autoload_playback_limit(0)`; length as intro + one loop, position folded past the loop, seeking
refused silently ("unstable"), tempo and tracks; and its volume upstream's own (`music_volume * internal / 100 /
20`, the volume limited to 18), because SDL_mixer hooks it past the music volume. Checked against the system's
libgme 0.6.5 through ctypes: sample-exact (correlation 1.000000) from the step after `tunes`, gain 0.80 at the
default volume. Upstream SRB2 vendors GME 0.6.1 for Windows and links the system's (0.6.3+) on Linux; 0.6.5 is
the current release. The gate plays GME's own `test.nsf` (from SRB2's `libs/gme`) from a PWAD it makes
(`waterbox/tests/make-wad.py`), through the console's `addfile` and `tunes`.

**The C++ guest toolchain** (miniBox `-Dguest_cpp=true`, `build/meson-cpp`): libstdc++ for the guest, linked
with miniBox's recipe (`--no-relax`, the weak `pthread` pulls, `cxxglue`). GME is built without exceptions or
RTTI (it uses neither), and with `-include ctime` (its `Hes_Emu.cpp` names `time_t` for an emulated time, which
glibc's headers declare in passing and musl's do not). On this host two workarounds were needed to build
miniBox's toolchain, both outside this repository: the system GCC is an Arch snapshot (16.2.1) with no release
tarball, so the build was seeded with 16.2.0's source; and libstdc++'s `std::stacktrace` (libbacktrace) fails
under GCC 16's C23 against musl's `basename()`, so it was configured `--disable-libstdcxx-backtrace`.

**The math is the core's** (`platform/detmath.c`, from the DSDA core): libvorbis builds its tables with `sin`,
`cos`, `acos`, `atan`, `exp` and `log`, and glibc and musl differ in their last bits, so the link answers them
with functions built from IEEE-exact operations only, in both builds. The same wraps cover what SRB2 itself
calls: **`hypot` in its slopes (play)** and `sincos` in its renderer (the picture), which the gate's
Greenflower 1 has none of, and which would have split native and sandbox on a sloped level. **`pow`** is the
core's too (GME's equalizer and filters take fractional powers): an integer exponent is repeated squaring,
exact wherever the result fits a double - so Lua's `^` (integer powers of integers) is what the C library
gives - and a fractional one goes through `exp`/`log`.

## The game's home

**SRB2's folder is the machine's root, never a folder of the host's.** `Init` starts the engine with
`-workdir .` (patch 0003) and `-home .`. Everything SRB2 keeps is built from that folder (`srb2home`), so its
files are the machine's own top-level names: `config.cfg`, `gamedata.dat` (unlocks, records), `srb2sav*.ssg`
(saves), `autoexec.cfg`, `replay/<folder>/MAPxx-*.lmp` (record attack), `luafiles/`, `addons/`. Without
`-workdir`, the folder would be `./.srb2/`, a subfolder the flat, name-matched mounts of miniBox would have to
spell. `-home` is still required (SRB2 stops without a user home) and is otherwise unused.

Nothing can reach the host's `~/.srb2` in either build. The core's `I_GetEnv` answers no `$HOME`, and paths
are relative to the machine's root: the mounts in the sandbox, the harness's work folder natively. Writes go
to the machine's memory (below) and `I_mkdir` makes nothing on a disk. Verified: a `config.cfg` at the work folder's root
is executed (`Executing ./config.cfg`) and changes the picture identically in both builds.

## The machine's filesystem (milestone 3)

`platform/files.c`, in both builds, wrapping the engine's `fopen`, `access`, `stat`, `remove`, `fileno`,
`fstat` and `opendir`:

- **A file the game writes is a memory file**: `malloc`'d guest memory behind an `fopencookie` FILE, so it is in
  every savestate and rewinds with the machine (Chimera's `docs/save-data.md`: never host-side, never
  invisible). From then on it shadows a mount of the same name; a mount opened to update or append is copied
  in first. A file written during `Init` (an `autoexec.cfg`'s, the game data read at start) is in the sealed
  baseline, so a state carries only what changes after.
- **Nothing reaches the host**: no write goes to a mount or a host file, and `opendir` lists nothing (natively
  it would list the work folder). Folders are implicit: `I_mkdir` records one, and `stat` calls a recorded
  folder, or one a file is in, a folder.
- **Memory files have descriptors of their own** (`fileno` answers one above any real one, `fstat` answers
  for it): `fopenfile`, which opens every file the engine reads, refuses what `fstat` does not call regular.
- **The save data export** (`GetSaveDataFileCount/Name/Size/Buffer`) is every memory file but `config.cfg`:
  `gamedata.dat` (unlocks, emblems, records), the save slots (`srb2sav*.ssg`), record attack's replays
  (`replay/...`), Lua's files. The harnesses' `--savedata-out <dir>` writes it, as `chimera-run
  --export-savedata` does.
- **Taking it back in** is the package's (milestone 7): a `savedata` slot whose files are mounted by the names
  the export gives, so SRB2 reads them itself at start (`gamedata.dat` in `G_LoadGameData`, a save slot when it
  is loaded) - before seal, as the contract asks. Several files are a zip (the export of more than one is), so
  the core will unpack one into memory files in `Init`.
- **`-warp` is a cheat to SRB2** for a map not yet visited (`M_CampaignWarpIsCheat` → `G_SetUsedCheats`):
  the game data is then neither loaded nor saved, as in the game. A movie that wants its game data starts
  from the title.

The gate's leg `files` mounts an `autoexec.cfg` that writes a file at start (`saveconfig mine.cfg`, before
seal), reads it back (`exec mine.cfg`) and writes another 20 tics into play (`wait 20`, `saveconfig late.cfg`,
after seal): the export is those two, byte for byte the same native, sandboxed, rerecorded and in a new
host, and the work folder is untouched. A real `gamedata.dat` write needs a level finished: input.

What the folder will hold in Chimera's terms:
- `config.cfg` is not the user's: nothing mounts one unless the project declares it (the frontend mounts only
  declared firmware, slot files and the settings), so a host's SRB2 configuration can never leak in; the
  settings that shape the machine come from the project. What SRB2 writes there stays in memory and is not
  exported.
- `gamedata.dat` is a project file (unlocked characters, emblems and levels change what the game offers): from
  the `savedata` slot, or none (a fresh game), and exported as save data.
- Saves and replays are written into the machine's memory, so they are part of the savestate (done).

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
