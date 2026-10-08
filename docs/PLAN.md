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
- [x] **5. Audio** (user-decided: a C mixer of the core's own; libvorbis, not stb_vorbis; GME and libopenmpt -
  libxmp if libopenmpt proves impractical; no MIDI for now):
  - [x] **5a.** The mixer (`platform/i_sound.c`): sound effects (DMX, WAV, Ogg Vorbis), Ogg Vorbis and WAV
    music with loop points and fades. Gate leg `audio`; every leg's run line carries the sound's hash.
  - [x] **5b.** GME (libgme 0.6.5: VGM/VGZ, NSF, SPC, GBS, HES, KSS, AY, SAP, GYM), music and sound effects,
    as upstream's mixer drives it. C++: the guest is built with miniBox's C++ toolchain.
  - [x] **5c.** libopenmpt 0.8.9 (tracker modules: MOD, S3M, XM, IT, MPTM...), as upstream's mixer drives it,
    its random seeding made deterministic (`patches/openmpt/0001`).
- [x] **6. Savestates**: rerecord and session pass (gate leg `savestates`, ~32 MB a state), including a state
  taken mid-wipe with the engine suspended on its cothread.
- [x] **7. The package and CI**: `build-package.sh`, the declaration (`waterbox/waterbox.config`: controller,
  firmware, video, audio, settings), file slots (add-ons, save data), keybinds, licences, CI
  (`.github/workflows/chimera.yml`, see "CI"). Gate legs `declaration`, `settings`, `exports`, `engine`.
  Confirmed in use (2026-10-05, user): on Windows the core imports into Chimera from GitHub, and a movie records
  and replays, savestates included.
- [x] **8. SRB2's OpenGL renderer** (user-decided 2026-10-05: both paths, Mesa first, as Chimera's PCSX2 core):
  - [x] **8a.** `renderer` `opengl`: upstream's renderer (`hardware/`, unchanged) on Mesa's softpipe compiled
    into the guest (`waterbox/setup-mesa.sh`, `platform/ogl_chimera.c`). Deterministic and savestate-safe;
    slow. Gate leg `opengl`. See "OpenGL".
  - [x] **8b.** `renderer` `opengl-hw`: the same renderer through Chimera's GPU bridge, onto the machine's
    GPU, falling back to the Mesa path when there is no bridge: `platform/gl_compat.c` (GL 1.x on the bridge's
    core profile), the rebuild when the context moves (`chimera_gl_step`, patch 0008). Gate: the `engine` leg's
    GPU checks. Setting `glShaders` (`gr_shaders`). See "OpenGL".

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
- `patches/openmpt/0001-deterministic-random-device.patch` (on `extern/openmpt`): libopenmpt's random seeding
  deterministic under `MPT_BUILD_DETERMINISTIC_RANDOM` (see "The sound").
- `0008-gl-context-lost.patch`: `ContextLost`/`ContextRestored` in the GL driver and `HWR_ContextLost`/
  `HWR_ContextRestored` in the hardware renderer: a platform layer whose context can be replaced under the
  renderer (the GPU bridge's) has it forget every GL name without deleting any, then make its objects again.
- `0007-gl-no-logfile.patch`: `GL_NO_LOGFILE`, for a platform layer with a console and no place for
  `ogllog.txt`: the OpenGL renderer's messages go to the console (as SDL's build has them), and no log file is
  written into the machine's files.
- `0006-all-maps-available.patch`: `menu_allmapsavailable`, which an external driver sets to list every
  map on the level platters as available, visited or locked or not (Unlock All Maps).
- `0005-maxvid-overridable.patch`: `MAXVIDWIDTH`/`MAXVIDHEIGHT` overridable by a build (the core's: 3840x2160).
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

## The package's declaration and settings (2026-10-04)

`waterbox/waterbox.config` is the package's declaration (Chimera's `WaterboxConfig`): one machine
(`systemId` SRB2, `kind` game), the controller (srb2-input.c's buttons and axes, in its order - the gate's
`declaration` leg holds them to the core's own `GetButtonName`/`GetAxisName`), the video (320x200 shown at 4:3,
35 Hz), the audio (1260 frames a step at 44.1 kHz), lag, the memory layout, the four pk3s as firmware (2.2.15's,
by SHA-1; the engine checks srb2, zones and characters by MD5 itself), and the settings.

**The settings are SRB2's own options**, given to the engine as `+` launch parameters by `Init`: upstream runs
those after its configuration and `autoexec.cfg` (`M_PushSpecialParameters`, on the title's path and on
`-warp`'s), so they are the machine's whatever a configuration says. User-decided defaults (2026-10-04),
which differ from SRB2's own:

| Setting | SRB2 option | Default (SRB2's own) |
|---|---|---|
| Play Style | `directionchar` + `configanalog`, as the play style menu sets them (`M_HandlePlaystyleMenu`): Strafe (Camera, Off), **Manual** (Movement, Off), Automatic (Movement, On), Old Analog (Camera, On) | **Manual** (Automatic) |
| Camera Speed | `cam_speed`, 0..1, clamped, printed at 5 places | **1.0** (0.3) |
| Camera Distance | `cam_dist`, the Manual and Strafe styles' camera (Automatic and Old Analog use `cam_simpledist`) | **192** (192) |
| Camera Height | `cam_height`, likewise (`cam_simpleheight`) | **40** (40) |
| Score/Time/Rings | `timerres`: Classic, Centiseconds, **Mania**, Tics | **Mania** (Classic) |
| Flip Camera with Gravity | `flipcam` | **Yes** (No) |
| Automatic Braking | `autobrake` (given explicitly, so a configuration cannot turn it off) | **On** (On) |
| Tutorial Prompt | `tutorialprompt` (the title's "play the tutorial?") | **Off** (On) |
| Unlock Record Attack, NiGHTS Mode and Marathon Run | the game data's `SECRET_RECORDATTACK` and `SECRET_NIGHTSMODE` unlockables (SRB2 offers Marathon Run whenever Record Attack is unlocked, `m_menu.c`) | **Off** |
| Unlock All Characters | the `SECRET_SKIN` unlockables (Amy, Fang, Metal Sonic) | **Off** |
| Unlock All Maps in Record Attack and NiGHTS Mode | the menu's check (`M_LevelAvailableOnPlatter`: visited, `M_MapLocked`) skipped, **patch 0006** - not the game data, so an addon's maps are listed too, no visit is recorded, and no "visit map" condition unlocks anything (28 Record Attack maps in 2.2.15) | **Off** |
| Unlock All Secrets | every unlockable (24 in 2.2.15) | **Off** |
| Start Map Character | `+skin`, given only with a Start Map (`-warp`), run before the map starts; a locked character (Amy, Fang, Metal Sonic) needs Unlock All Characters, and a locked or unknown one plays Sonic | **empty**: Sonic |
| Renderer | `software`, `opengl` (SRB2's OpenGL renderer on the core's Mesa softpipe), or `opengl-hw` (the same through Chimera's GPU bridge, on the Mesa when none is offered; see "OpenGL"), fixed at the start as upstream's `-renderer` | **software** (Software) |
| OpenGL Shaders | `gr_shaders`; the picture's alone | **On** (On) |
| Resolution | the engine's one video mode (see "Resolution") | **1280x800** (1280x800) |
| Start Map | `-warp` (empty: the intro and the title) | empty |

**The unlocks** (user-decided, 2026-10-04: so a verification movie need not be made for each run) are set in
the game data the engine has loaded, client's and server's (`srb2-driver.c`), at the start and before every
step: an add-on that loads game data of its own (`dehacked.c`) reloads it, and SRB2's own updates only ever
unlock, so re-setting them is idempotent. They are not cheats to SRB2 (`usedCheats` is untouched): a game with
them saves its game data, and the save data export carries them. The 1 Player menu shows Record Attack, NiGHTS
Mode and Marathon Run with the first on, `???` without it. With the tutorial prompt off, the gate's menu movie
(`tests/menu-to-new-game.txt`) goes from the title through Start Game, the save and character selects, into a new
game in Greenflower Zone.

Manual is the Standard control style (`PF_DIRECTIONCHAR`, no `PF_ANALOGMODE`): the player faces where it moves
and the camera does not turn by itself. The gate's `settings` leg reads the engine's options back
(`run-native --print-cvar`): the defaults absent and given, and every other value of every setting.

## OpenGL (milestone 8, 2026-10-05)

**The renderer is part of the game.** Not only the picture: `A_OverlayThink` (`p_enemy.c`) places overlays by
the viewing angle under OpenGL alone, and the orbital camera (`p_user.c`) depends on `gr_shearing` under
OpenGL. So the renderer is a project setting (`renderer`, recorded with the movie), and a movie is its
renderer's. Any two ways of running the OpenGL renderer share one game. The renderer is fixed at the start
(`chosenrendermode`, as upstream's `-renderer`); a switch the game asks for later - the video menu, a
configuration's `renderer` - is refused (`VID_CheckRenderer`).

**`HWRENDER` is in both builds**, and changes nothing in software: the native reference built with it and the
guest built without it gave the same Greenflower run (1000 steps: picture, sound, state). The native reference
has no GL (`LoadGL` fails, `ogl_chimera.c`), says "OpenGL did not start; drawing in software" and draws in
software - so the `opengl` leg is the sandbox's alone, as Flycast's GL legs are.

**8a, the Mesa path.** Chimera's porting guide's second way to a picture: an OpenGL compiled into the core.
`waterbox/setup-mesa.sh` is chimera-core-flycast's (MIT): Mesa 24.0.9 by SHA256, softpipe behind gallium
OSMesa, static, no LLVM - plus `lmsensors`, `libunwind`, `valgrind` pinned off (Mesa turned lm_sensors on from
this host and failed on a header the guest lacks) and a release build (Mesa's default `debugoptimized` keeps
assertions in softpipe's loops: the release build is 20-25% faster and the core 31 MB instead of 89).
`platform/ogl_chimera.c` is upstream's `sdl/ogl_sdl.c` and `hwsym_sdl.c` less SDL: the context
(`OSMesaCreateContextExt`, BGRA, 24-bit depth, 8-bit stencil), `GetGLFunc` by `OSMesaGetProcAddress`, the
surface at the mode's size with rows top first (`OSMESA_Y_UP` 0: the frontend's layout, no conversion), and
the finished frame copied out before the renderer draws the screen texture back (upstream does that after its
swap). Two names needed care: the renderer's `Init` is the core's export's name, so SRB2's objects build with
`-DInit=r_opengl_Init` (`SRB2_RENAMES`), and `GL_NO_LOGFILE` (patch 0007) keeps `ogllog.txt` out of the
machine's files.

Every byte of the GL - textures, shaders, the framebuffer - is guest memory: savestates need nothing (gate
leg `opengl`: rerecord and a mid-wipe session are the run without; deterministic run to run). Chimera's
engine opens the package with `renderer` `opengl` and draws the menus.

Its cost is softpipe's (no JIT, no SIMD dispatch, by design), measured per frame on Greenflower, this machine:

| resolution | shaders off | shaders on (SRB2's default) |
|---|---|---|
| 320x200 | 0.061 s | - |
| 640x400 | 0.174 s | 0.433 s |
| 1280x800 | - | 1.644 s |

Flycast's softpipe is ~0.12 s a 640x480 frame, so this is softpipe's normal range. It is a renderer for
checking and encoding a movie, not for playing one: that is 8b's.

**8b, the bridge.** Chimera's GPU bridge carries only the calls on miniBox's master list
(`source/gl/gl-entry-points.txt`), which has no fixed-function GL, and on Linux it asks EGL for a 3.3 context,
which is a core profile; SRB2's renderer is GL 1.x (matrix stacks, client-side arrays, `glTexEnv`,
`glAlphaFunc`, lights and materials for models: ~30 of its ~80 calls) and its GLSL uses the compatibility
built-ins. The core cannot change Chimera (an unofficial core), so the translation is the core's own,
`platform/gl_compat.c`, between `GetGLFunc` and the bridge:

- **matrices**: the three stacks kept in the core and handed to every program as uniforms (each program
  remembers the version it was given, so a draw sends only what changed); `glGetFloatv` of them answered here.
- **arrays**: client arrays streamed into buffers of the core's under a vertex array of its own, at fixed
  attribute locations (0 vertex, 1 colour, 2 and 4 texture coordinates, 3 normal); arrays in the renderer's own
  buffers (models, the sky) pointed at in place; client indices into an element buffer. A disabled array is the
  current value (`glColor4ubv`, `glMultiTexCoord2f`).
- **fixed stages**: with no program of the renderer's in use, one of the core's: the texture environment
  (modulate, replace) on two units (the wipes' fade mask), the alpha test, the one light models use.
- **GLSL**: the renderer's shaders rewritten for GLSL 3.30, whole word by whole word (`gl_Vertex`,
  `gl_ModelViewMatrix`, `gl_FrontColor`, `gl_TexCoord`, `gl_FragColor`, `texture2D`...), `#version` lines
  dropped, and the alpha test run after the shader's own `main` (renamed), as the fixed stage after a shader
  does. A program given no vertex stage gets one.
- **textures**: luminance-alpha and alpha formats as RGBA with a swizzle (the fixed stages know an alpha-only
  texture modulates alpha alone), `GL_GENERATE_MIPMAP` as `glGenerateMipmap`, `GL_CLAMP` as clamp-to-edge.
- **imageless textures**: GL 1.x draws a unit whose texture has no image as if texturing were off; a core
  shader sampling one reads black. The renderer binds such a name (`NOTEXTURE_NUM`) for flat fills, so the
  character select's backgrounds drew black on the GPU (found in use, 2026-10-05) until the fixed stages
  learned which textures have an image (`glTexImage2D`, `glCopyTexImage2D`). Gate: the `engine` leg compares
  the GPU's character select with the Mesa's.
- **strings**: `GL_EXTENSIONS` answered from `glGetStringi`; and each string kept in a buffer of its own -
  the bridge copies a returned string into one guest buffer, so `GL_VERSION` read back as the renderer's name
  and the mipmap check failed.
- **framebuffer**: a context made with no surface has no default framebuffer; the renderer draws into the
  core's, read back once a frame (`glReadPixels`, rows flipped).

The bridge's guest half is generated at build time by miniBox's `source/gl/gen-gl-bridge.py` from glad's
declarations (`waterbox/glad`, the copy Chimera's engine vendors) for the 88 entry points the core calls
(`waterbox/gl-bridge-list.txt`), with the master list's opcodes. The core exports `SetGpuBridge` (the engine
offers the bridge before `Init`) and `StateLoaded`.

**The GL is outside the machine.** A savestate brings back the renderer's object names, which the driver no
longer means - after a load in the same session (the engine mints a new context id on every load) and in a
new session (a project closed and opened again: the bridge's context outlives sessions). So at the top of
every step, a moved context id or a `StateLoaded` since makes the renderer forget and rebuild
(`chimera_gl_step`): every object the core ever made in the driver is deleted at once - their names are
listed where a state does not reach (`ECL_INVISIBLE`) - then the renderer forgets its names without deleting
any (patch 0008's `HWR_ContextLost`: the texture cache, light tables, screen and palette textures, shader
programs, the sky's and models' buffers), the core's own objects are made again, and then the renderer's
(`HWR_ContextRestored`: its states, the fallback shader, its shaders, palettes and model buffers; textures and
light tables as they are next used). In that order, so no stale name can delete an object made since.

`tests/engine-gpu-states.py`, through Chimera's engine on the GPU: a straight run, a rewind (state saved at
150, loaded at 250), and the state loaded into a second session; steps 151-300 of both must be the straight
run's pictures, each with one rebuild. Its teeth were watched: the core built without the rebuild differs at
steps 214 and 221 after the rewind (a stale name in the same level still names a texture by luck most of the
time) and from the first step after the reopen.

Measured (RTX 2070 SUPER, NVIDIA 615.71, Greenflower, through the engine, readback every frame):

| resolution | steps/s |
|---|---|
| 640x400 | ~250 |
| 1280x800 | 134 |
| 1920x1080 | 70 |
| 3840x2160 | 15-18 (the 33 MB readback dominates) |

The picture is the driver's: the session says it is not deterministic, and Chimera's movie names the driver.
The game is not: `opengl` (the Mesa) and `opengl-hw` give the same sound over the same run, and pictures that
differ in 0.08% of pixels (rasterisation at edges) with shaders and without. With no bridge offered (or one
that will not start), `opengl-hw` draws on the Mesa and says so.

**`glShaders`** (`gr_shaders`) is a setting: the renderer's shaders, or its fixed stages - about 2.5 times
faster on the Mesa, and the picture's alone.

Not done: `glShadeModel(GL_FLAT)` (the renderer leaves it after drawing a model) is drawn smooth - the sky
dome's colours after a model, at most; `video.drawEveryFrame` is not needed (the core exports no
`SetRenderingEnabled`, so it always draws). Windows makes the bridge's context with `wglCreateContext`, a
compatibility profile, where everything here is equally valid; not yet run there.

## Resolution (2026-10-04, user-decided)

**The Resolution setting** offers SRB2's own video modes (`sdl/i_video.c`'s `windowedModes`, 320x200 to
1920x1200) and 2560x1440 and 3840x2160 beyond them; the default is 1280x800, SRB2's own (`scr_width`/`scr_height`).
The engine's one mode is that one (`chimera_video_set_mode`, before the start): whatever mode a configuration or
the video menu asks for, it gets the setting's. SRB2's renderer sizes its tables by `MAXVIDWIDTH`/`MAXVIDHEIGHT`
(1920x1200), so **patch 0005** lets a build raise them, and the core builds with 3840x2160.

**Square pixels**: SRB2 is a modern game and is shown at the size it draws - `GetDisplayAspectX/Y` answer the
mode's own width and height, not 4:3.

**No sync difference between resolutions**: the gate's `resolution` leg plays Greenflower's movie at 320x200,
1280x800, 1920x1080 and 3840x2160, native and sandboxed, and finds one game state (the new state digest,
`GetStateDigest`: the tic, level time, RNG seed, game state and map, the player, the camera, and every object's
position, momentum, angle, type, state and health, every step), one sound, and four pictures. Reading the
engine, nothing of play reads the screen's size (only the player's eye height, `viewheight`, which is not the
screen's). 3840x2160 costs about 9x the time of 320x200 natively (13 s for 260 steps).

## The controller (2026-10-04, user-decided: keys + analog axes)

A movie row is **SRB2's own keyboard plus four axes**:

- **The buttons are the game's controls in its default keyboard scheme** ("FPS", `gamecontroldefault[gcs_fps]`):
  Forward (W), Backward (S), Strafe Left/Right (A/D), Turn Left/Right (←/→), Look Up/Down (↑/↓), Jump
  (Space), Spin (Left Shift), Center View (Left Ctrl), Camera Reset (R), Camera Toggle (V), Custom 1-3 (Z/X/C),
  Pause (P), and the menus' Enter and Escape (a prompt's yes and no too: Enter or Space, Escape; the Y and
  N buttons were dropped 2026-10-05, user-decided, as redundant). **The ring-slinger controls - Fire,
  Fire Normal, Toss Flag, Weapon Next/Prev, Weapon 1-7 - are commented out for now** (user-decided, 2026-10-04:
  multiplayer's, and they crowded the UI); their keys keep SRB2's bindings and are never pressed.
- **What the controls are called is the core's** (2026-10-06, after Chimera's 74d25e1 "Chimera knows no
  system": it dropped its `MnemonicLookup` and `SystemNames` tables). `waterbox.config` declares
  `systemNames` (SRB2 is "Sonic Robo Blast 2"), `mnemonics` (a letter for every button: Doom's directions,
  ^ v < > for moving and { } for turning, U/D looking, J S C R T, 1-3, P, Enter E and Escape e) and a
  `header` on every axis (Fwd, Side, Turn, Aim). `build-package.sh` refuses a package whose buttons lack a
  letter, share one or have one an entry cannot carry (`.`, `|`, non-ASCII), or whose axes lack a header.
  The button names are the natural ones again - Turn Left/Right, Escape - no longer bent so that the
  frontend's fallback (a name's last word) would differ; no compatibility is kept with movies under the old
  names (user-decided: none exist yet).
- **Default keys** (`default_keybinds.json`) are Chimera's merged modifiers: Spin is Shift and Center View Ctrl
  (Chimera binds Shift, not LeftShift, unless asked to tell them apart). A button that changes is a key event (`D_PostEvent`), so the menus, the
  title, a prompt and the game read it as a keyboard, and the game builds its tic command with all its own
  logic (accelerative turning, the Simple style's camera, Lua's `PlayerCmd`). In menus the arrows are Look
  Up/Down and Turn Left/Right, as on a keyboard; a yes/no prompt takes Enter as yes and Escape as no.
- **The bindings are forced to that scheme after the start** (`srb2_input_bind`): a configuration or an
  `autoexec.cfg` cannot change what a recorded button means.
- **The axes** go in through upstream's seam for an external driver, `I_BaseTiccmd` ("empty, or external
  driver"), the command `G_BuildTiccmd` starts from: Forward Move and Side Move (-50..50), Turn (an angle delta
  in 1/65536 turns, added to the turn keys'; positive turns right), Aim (the look pitch; positive looks down,
  as a stick's Y; 0 leaves the game's own look). Both are the command's negated (its angle counter-clockwise,
  its pitch up). The game adds
  its keys' movement to the base without clamping the sum, so a movement axis counts only while its keys are
  not held. `G_BuildTiccmd` overwrites the command's aiming with its own look state (which springs back to
  level), so **patch 0004** lets a non-zero base aiming set the pitch.
- `FrameAdvance`'s mask is the first 64 buttons; `SetButton` and `SetAxis` the rest; `GetButtonName`/
  `GetAxisName` name them, in the controller's order (for the declaration, and the harnesses' `--input`).
- Not yet: player 2 (splitscreen), text entry (a name, the console), the joystick-style analog
  configuration. The play style, the camera speed and the camera's flip are settings (see "The package's
  declaration and settings"); SRB2's other options that shape the tic command are its defaults.

The harnesses take a movie as text (`--input FILE`: `FROM-TO: Button; Axis=value` a line);
`waterbox/tests/` holds the gate's two.

## Game State and the TAS info script (2026-10-08, user-asked)

The user asked for a Lua TAS info script (`lua/tasinfo.lua`): speed, the angle in hex and in decimal to 4 places
(on lines of their own), the position and the momentum on three axes, and the speed shoes, invincibility, space
and air timers; then (the same day) the conveyor and platform momentum on three axes, and a spindash rev counter,
with the conveyor and platform momentum, the revs and the timers hidden while 0; then the boss's flashing and
health, and Metal Sonic's dash mode, after the SRB2 TAS build (the user's `TASBuild.2215.patch`). Until then the core exposed no memory at all, so a script had nothing to read: the core now has
the `Game State` domain and a property table (Chimera's `docs/game-cores.md`, "Properties"), as the DSDA core
has. Decisions:

- **A copy, read-only**, made at the end of every step (`srb2-driver.c`, `update_game_state`), as DSDA's: the
  fields are what the game works out afresh each tic, so a poke would do nothing, and the copy cannot change
  the game - the run's hashes are unchanged. It is in the machine's memory, so a savestate carries it.
- **The player's object** (`players[consoleplayer].mo`) for the position, momentum and angle; `player->speed`
  for the speed, the game's own number (`P_AproxDistance` of the momentum against the floor, so it is not the
  Euclidean length); `powers[]` for the timers; `player->cmomx`/`cmomy` (what a conveyor or a platform carrying the player adds;
`momx - cmomx` is the player's own) and `mo->pmomz` (the moving floor's vertical momentum) for the conveyor and
platform momentum. **A spindash rev is a tic of charge**: SRB2 has no discrete revs (Sonic 2's); revving adds
1.0 to `player->dashspeed` every tic Spin is held, from the skin's `mindash` to its `maxdash`, and the rev sound
marks sixths of that. So the table carries `dashspeed`, `mindash`, `maxdash` and `pflags` (with `PF_STARTDASH`
as a named bit, `Player.Charging Spindash`, its bit worked out from the header), and the script counts
`(dashspeed - mindash)` in whole units while charging: revs so far / revs to full. (The SRB2 TAS build counts
its revs otherwise - the rev sound's sixths, 1 to 7; that was not taken over.)
- **The boss as the SRB2 TAS build finds it** (`P_GetBossInfo`): the first `MF_BOSS` object among the
  thinkers; its health, flashing while `MF2_FRET` (which each boss's states clear, so there is no flash timer
  to show), and - beyond the build - its type's `spawnhealth`, for health as left / at the start.
- **Metal Sonic's dash mode as the build shows it**: `player->dashmode` (up a tic at top speed to
  `DASHMODE_MAX`, 108; dash mode from 105; down 3 a tic below it), and at 108 `player->normalspeed`, the top
  speed dash mode raises. Out of a level all of them are 0 and `Player.In Level` false.
- **The raw values**: 16.16 fixed point, the 32-bit angle, tics. The script turns them into units, degrees
  and seconds; RAM Watch shows them as they are, as a TASer of SRB2 is used to.
- The script reads by name (`game.get`), never by offset, and says so in the console when the core has no
  such properties (a package from before them).

Gate leg `properties`: the harnesses' `--game-state` writes every step's block and the table; on Greenflower's
movie, read by the table, the position moves with Forward, the jump lifts, the angle turns only with the Turn
axis, and in the air the speed is the momentum's `P_AproxDistance` exactly; with `tests/timers.lua` loaded (an
SRB2 Lua add-on that sets the four timers after the game's think) every timer and the conveyor and platform momenta are what it set, every step; on `tests/spindash.txt` (Spin
held 40 tics from a standstill) the charge starts at Min Dash, rises 1.0 a tic, and launches the player at it;
with `tests/boss.lua` loaded (an Egg Mobile spawned at leveltime 30 and hit at 40; dash mode set every tic) the
boss reads 8/8, then 7/8 and flashing until its pain state ends (leveltime 80), and dash mode what it set;
native == sandbox == rerecord byte for byte. Teeth: the table with angle and speed swapped, air and space,
dash speed and max dash, or boss health and max health, fails. Metal Sonic's real counter was watched counting
(Greenflower straight ahead: 4, 24, 40, then down at a wall) and the script's 108 line with a forced value. The script itself was run under Lua 5.4 with `game`, `gui` and `emu` stubbed over the leg's dumps
(in a level, out of one, the timers running): it was not run in Chimera's Lua Console here. On the development host a libchimera
built there crashes opening the package (2026-10-08: f39db76's and c7c06d7's alike, though c7c06d7 passed two
days before with the same core.wbx); a CI-built bundle's (e51a141) opens it and plays the engine leg's 450
steps with the hashes that passed then - so the crash is the host's build of the engine, not the core.

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

**libopenmpt** (`extern/openmpt`, libopenmpt-0.8.9, a shallow submodule of OpenMPT's repository) is built from
its own Makefile's source list (the library, its common and sound code, the DMO plugins it emulates; C++17 with
exceptions and RTTI, as it is built), with the core's Ogg Vorbis and zlib for MO3's compressed samples and
archives. Its own build settings leave SIMD intrinsics out of a library build, so nothing is chosen by the host's
CPU. **Its random seeding is not**: every module draws its generator's seed from a global one seeded by
`std::random_device` (tracker effects with randomness - IT's random volume and panning, random vibrato
waveforms - would play differently each load). The fuzzer build's deterministic engines also change loaders and
the resampler, so **patch `patches/openmpt/0001`** makes only the seeding device the deterministic one, under
`MPT_BUILD_DETERMINISTIC_RANDOM`, with the real generators. `apply-patches.sh openmpt` applies the series as it
does SRB2's. SRB2 is built with `HAVE_OPENMPT`: its `s_sound.c` keeps the module handle (`openmpt_mhandle`, which
the mixer uses, so the Instrument Filter setting reaches the playing module as upstream's does) and its sound
menu has the OpenMPT section, as upstream's Linux builds have. The mixer does what upstream's does with a
module: probed after GME and before Vorbis, subsong 0, the interpolation filter from `cv_modfilter`, repeat
forever when looping, tempo (limited to 4), subsongs, length and position (seeking adjusted for the length),
and the GME volume scale. Checked against the system's libopenmpt (0.5.5) through ctypes: correlation 1.0000 on
OpenMPT's own `test.mod`, gain 0.80. The gate plays it from a PWAD it makes (`test.xm` and `test.mptm` are
silent feature tests).

**The C++ guest toolchain** (miniBox `-Dguest_cpp=true`, `build/meson-cpp`): libstdc++ for the guest, linked
with miniBox's recipe (`--no-relax`, the weak `pthread` pulls, `cxxglue`). GME is built without exceptions or
RTTI (it uses neither), and with `-include ctime` (its `Hes_Emu.cpp` names `time_t` for an emulated time, which
glibc's headers declare in passing and musl's do not). On this host two workarounds were needed to build
miniBox's toolchain, both outside this repository: the system GCC is an Arch snapshot (16.2.1) with no release
tarball, so the build was seeded with 16.2.0's source; and libstdc++'s `std::stacktrace` (libbacktrace) fails
under GCC 16's C23 against musl's `basename()`, so it was configured `--disable-libstdcxx-backtrace`.

**The math is the core's** (`platform/detmath.c`, from the DSDA core): libvorbis builds its tables with `sin`,
`cos`, `acos`, `atan`, `exp` and `log`, libopenmpt its resampler and filters with their float forms (`sinf`,
`cosf`, `sincosf`, `tanf`, `logf`, `log10f`, `powf`, and `log2`, exact on a power of two), and glibc and musl differ in their last bits, so the link answers them
with functions built from IEEE-exact operations only, in both builds. The same wraps cover what SRB2 itself
calls: **`hypot` in its slopes (play)** and `sincos` in its renderer (the picture), which the gate's
Greenflower 1 has none of, and which would have split native and sandbox on a sloped level. **`pow`** is the
core's too (GME's equalizer and filters take fractional powers): an integer exponent is repeated squaring,
exact wherever the result fits a double - so Lua's `^` (integer powers of integers) is what the C library
gives - and a fractional one goes through `exp`/`log`.

## CI (2026-10-04)

`.github/workflows/chimera.yml`, modeled on the DSDA core's: one `gate` job and the `publish` job every core
publishes with (Chimera's reusable `publish-core.yml`: a rolling `dev` on every green push to main, a dated
`nightly-YYYY-MM-DD` from the 04:23 UTC schedule when main moved; off the hour, where GitHub drops fewer scheduled
runs - 04:00 never fired - and by hand: Actions > Run workflow, kind `nightly`).

The gate job checks out this repository (submodules; `extern/openmpt` shallow) and Chimera (`CHIMERA_REF`,
main), builds Chimera's native libraries and solution, builds miniBox's host and C++ guest toolchain (cached;
the toolchain's targets named, as they are not in miniBox's default target), **fetches SRB2 2.2.15's data from
STJr's own release** (`waterbox/fetch-data.sh`: `SRB2-v2215-Full.zip` from github.com/STJr/SRB2, the four pk3s
checked against `waterbox.config`'s SHA-1s - they match; cached), builds the native reference and the core,
runs `run-gate.sh` with the engine leg on the checkout's installed `libchimera`, builds the package stamped with
the commit, runs Chimera's contract tests on it (`InstalledCorePackagesTests`, `MnemonicUniquenessTests`), and
uploads `srb2-<sha>`.

**Every release carries `tasinfo.lua`** (user-asked, 2026-10-08): the `publish-lua` job, after `publish`, uploads
`lua/tasinfo.lua` into the release just made and adds a line to its notes. Chimera's `publish-core.sh` uploads
only the package and deletes and recreates `dev` each time, so the script cannot ride along in it and must go up
after; and the shared workflow is the Chimera project's, so the job is this repository's own. The release is the
one `publish-core.yml` picks (a nightly for the schedule or kind `nightly`, else `dev`); a nightly is found as the
newest `nightly-*` tag at the commit by name - the API's tags are lightweight, their creator date the commit's.
The gate job checks the scripts parse (`luac5.4 -p`). The job's shell was run against a scratch repository with a
stand-in `gh` (dev, nightly, two nightlies at one commit, none); the workflow itself first runs on GitHub.

**It depends on no Chimera change.** This is an unofficial core (github.com/P-AS/chimera-core-srb2,
user-decided 2026-10-04): it is not in Chimera's list of cores (`official-cores.json`, and the README's table),
which is the Chimera project's to grant. Since Chimera's ecc06b8 (2026-10-07) that list makes no difference to a
user: Chimera ships no cores and downloads nothing, the Core Manager lists the packages in the cores folder, and
every core, official or not, gets there because somebody put its package there (from its releases page, or built).
The core was adapted for that at Chimera f39db76: the README's "Using it in Chimera", `docs/BUILDING.md` and
`AGENTS.md`, after the DSDA core's d994c4f; the build, the package and the publish job were unchanged.

Since Chimera's 74d25e1 a package says what its system and its controls are called, so nothing about SRB2 needs
a Chimera change: `waterbox.config`'s `systemNames`, `mnemonics` and axis `header`s (see "The controller") are
what TAStudio, the movie text and the system lists show. Chimera's
contract tests from its stock main run on the package, `MnemonicUniquenessTests` among them (which now holds a
core that declares letters to declaring all of them, each writable). The former `docs/chimera-proposal.patch`
(a `MnemonicLookup` entry and a `SystemNames` entry for SRB2) is gone: both tables left Chimera, and what it
proposed is the core's declaration now.

Checked before any push: the job's core steps in an `ubuntu:24.04` container (GCC 13.3, as `ubuntu-latest`) -
miniBox's C++ toolchain from scratch, the data fetched and checked, both builds, the whole gate, the package.
GCC 13 knows C23 as `gnu2x`, so the engine is built `-std=gnu2x` (every later GCC accepts it too). Every hash the
gate prints - pictures, sounds, game states, at every resolution - was the same there as on the GCC 16 host: two
compilers, one machine. Two things that run surfaced, both fixed in the gate: a relative `-d` made the work
folders' links to the data dangle (it is made absolute), and a run that printed nothing ended the gate silently
under `set -e` (an empty run is now a leg's FAIL).

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
