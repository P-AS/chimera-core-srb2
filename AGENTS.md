# AGENTS.md - SRB2 core for Chimera

This repository builds Sonic Robo Blast 2 (Sonic Team Junior's 3D Sonic
fangame) as a GAME core (one game engine built as a core, not an emulator)
for Chimera (https://github.com/ToolAssisted-run/chimera), a frontend for
tool-assisted speedruns. It is an UNOFFICIAL core
(https://github.com/P-AS/chimera-core-srb2): the Chimera project does not
maintain it, and nothing here may depend on a change to Chimera. It produces
one file, `srb2.chimeraCore`: the engine as a sandboxed guest (`core.wbx`)
and its declarations. The game's data (srb2.pk3, zones.pk3, characters.pk3,
music.pk3) is never in the repository or the package: it is the user's
firmware.

## Layout

- `extern/SRB2`: upstream SRB2 (master, net-compatible with 2.2.15), a
  submodule. Never edited in place. `extern/ogg`, `extern/vorbis`,
  `extern/gme`, `extern/openmpt` (shallow): library submodules.
- `patches/`: the numbered patches for `extern/SRB2`; `patches/openmpt/`: the
  one for `extern/openmpt`. Both applied by `waterbox/apply-patches.sh`.
- `waterbox/sources.mk`: the sources and defines both builds share.
- `waterbox/guest.mk`: builds `build/guest/core.wbx` (the sandboxed core).
- `waterbox/native.mk`: builds `build/native/run-native` and `run-wbx` (the
  native reference, and the harness that runs `core.wbx` as the frontend
  does).
- `waterbox/setup-mesa.sh`: builds Mesa's softpipe for the guest
  (`build/mesa/`), which the OpenGL renderer draws on and both builds take
  their GL headers from.
- `waterbox/fetch-data.sh`: downloads STJr's 2.2.15 release and takes out the
  four pk3s, checked (the gate's data where none is installed).
- `waterbox/build-package.sh`: builds the package. `run-gate.sh`: the gate.
- `waterbox/srb2-driver.c`: the machine, and the Game State domain and its
  property table (`GetGameProperties`). `srb2-input.c`: the controller.
  `wbx-entry.c`: the exports. `platform/`, `compat/`, `native-shim/`: what
  stands in for SRB2's SDL code. `platform/gl_compat.c`: the GPU bridge's
  guest half.
- `waterbox/waterbox.config`, `default_keybinds.json`, `file_slots.json`: the
  declaration, written by hand. `package-licenses.json`: the licence terms the
  package carries.
- `waterbox/tests/`: the gate's movies and helper scripts
  (`check-properties.py` and `timers.lua`: the properties leg).
- `lua/`: Lua scripts for Chimera's Lua Console: `tasinfo.lua`, the TAS info
  overlay. They read the core's Game State properties by name (`game.get`).
- `.github/workflows/chimera.yml`: CI. It gates, packages and publishes.
- `build/`: every output. Ignored by git.

## Set up the build environment

Linux only (CI: `ubuntu-latest`). `<chimera>` is a Chimera checkout and
`<minibox>` is `<chimera>/extern/chimera-common-minibox`.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip curl bison flex python3-mako python3-packaging

git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

# Chimera itself: libchimera (the gate's engine leg) and the contract tests.
# The dotnet line needs the .NET SDK 8.0.
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

# miniBox: the sandbox host and the C++ guest toolchain (GME and libopenmpt
# are C++). Its guest targets are not in its default target, so they are named.
mb=<minibox>
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
ninja -C "$mb/build/meson-linux" source/guest/emulibc.c.o
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
ninja -C "$mb/build/meson-cpp" libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
```

Every script and makefile takes miniBox from `MINIBOX_DIR` (or `-m <dir>` for
the scripts, `MB=<dir>` for `make`). Export it once:
`export MINIBOX_DIR=<minibox>`. Nothing falls back to a default place.

## Build

From the repository root, in this order:

```
./waterbox/setup-mesa.sh                    # once, ~15 minutes: build/mesa/
make -C waterbox -f native.mk -j"$(nproc)"  # build/native/run-native, run-wbx
make -C waterbox -f guest.mk -j"$(nproc)"   # build/guest/core.wbx
./waterbox/build-package.sh -r <chimera>    # <chimera>/build/Cores/srb2.chimeraCore
```

The makefiles apply the patches on the first build. `build-package.sh`
builds the guest itself; the gate does not build anything but the package
for its engine leg, so build both flavours before it.

`build-package.sh -r <chimera>` writes `<chimera>/build/Cores/srb2.chimeraCore`
(for a Chimera bundle, a folder with `Chimera.exe`: its `Cores`) and clears
the compiled-core cache for this package. `-o <dir>` writes
`<dir>/srb2.chimeraCore`. With neither the package is
`build/package/srb2.chimeraCore`.

A hand-built package stamps its version `<commit>+local` (`-dirty` when the
tree has changes) and is for testing. CI stamps the commit through
`CORE_VERSION` and publishes the releases.

## Install the core into Chimera

Chimera ships no cores and downloads nothing: a core is a file in its cores
folder. In a source checkout that is `<chimera>/build/Cores/`, where
`build-package.sh -r <chimera>` writes. In a release bundle it is the `Cores`
folder beside `Chimera.exe`, or the one chosen in File > Core Manager >
Change folder... File > Core Manager lists the folder; Refresh List rescans
it. The same package works on Linux and on Windows. A project brings the four
pk3s as firmware.

## Test before you commit

The gate must end with `gate: all legs pass` (it exits non-zero otherwise).
It needs SRB2 2.2.15's data; `./waterbox/fetch-data.sh` puts it in
`build/srb2-2.2.15` (network access, once):

```
./waterbox/run-gate.sh -d build/srb2-2.2.15 -c <chimera>/build
```

`-d` defaults to `/usr/share/games/SRB2`. `-c` (a Chimera checkout's
installed `build/`, or a bundle) adds the engine leg: the package through
Chimera's `libchimera`, and `opengl-hw` through its GPU bridge where the
machine gives a GL context (otherwise a SKIP line, not a failure).

Then, with the package in `<chimera>/build/Cores`, Chimera's contract tests:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Rules of this repository

- Nothing may depend on a change to Chimera: build against its stock `main`.
  The core says what its system and its controls are called
  (`waterbox.config`'s `systemNames`, `mnemonics`, axis `header`s).
- `extern/SRB2` is a submodule. A change to it is a numbered patch in
  `patches/`, applied by `waterbox/apply-patches.sh` (`patches/openmpt/` and
  `apply-patches.sh openmpt` for libopenmpt). Never commit inside a
  submodule. The script takes a series as a whole: a pristine tree gets all
  of it, a fully patched tree is left alone, anything else is refused with
  the command to reset the tree. Keep the patches small hooks: upstream moves
  (2.2.16), and patch 0003 drops then.
- `build-package.sh` refuses a package whose `default_keybinds.json` and
  declared buttons disagree, or whose button letters, system name or axis
  headers are missing: change `waterbox.config` and the keybinds together.
- A Game State property's name is what RAM Watch, freezes and scripts store
  (`lua/` among them): rename or remove one only with everything that uses
  it. Its offset may move; `check-properties.py` reads by the table.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it (equivalence, steps, savestates, time,
  opengl).
- Run the gate before committing. A new leg needs teeth: show it fails when
  the thing it checks is broken (every leg in `run-gate.sh` names its teeth;
  Chimera's `docs/gates.md` says why).
- Never commit the game's data or any other game file. `build/` is ignored by
  git. Never add network access to the core (patch 0002 keeps SRB2's curl
  out).
- Shell scripts stay executable (git mode 100755): `waterbox/*.sh`.
- Documentation prose is plain ASCII.
- Commit messages: the subject states what is now true, often after a topic
  and a colon ("controller: the core says what its system and its controls
  are called"). The body says what changed and why.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md`: every option, the files a user provides, troubleshooting.
- `README.md`: the settings, the controller, the gate's legs, the licences.
- `docs/PLAN.md`: the milestones, the decisions behind the core, and what is
  left.
- The header comments of the scripts and makefiles in `waterbox/`.
- Chimera's `docs/`: `game-cores.md`, `porting-a-core.md`, `core-manager.md`,
  `gates.md`.
