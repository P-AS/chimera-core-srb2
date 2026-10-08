# Building the SRB2 core

This repository builds Sonic Robo Blast 2 as a Chimera game core: one game
engine built as a core, not an emulator (Chimera's `docs/game-cores.md`). It
is an unofficial core, not maintained by the Chimera project. The result is
one file, `srb2.chimeraCore`, which Chimera loads; the game's data is the
user's. The steps below follow `.github/workflows/chimera.yml`, which builds
and gates the core from a fresh clone on a public Ubuntu runner.

Placeholders used below:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, Chimera's miniBox
  submodule (the sandbox host and the guest toolchain).

## Requirements

- Linux. CI runs on GitHub's `ubuntu-latest` runner. Cores are built on Linux;
  the package they produce runs on Linux and on Windows.
- The apt packages the workflow installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip curl bison flex python3-mako python3-packaging
  ```

  `bison`, `flex`, `python3-mako` and `python3-packaging` are Mesa's build
  needs; `curl` fetches Mesa and the gate's data.
- The .NET SDK 8.0 (the workflow uses `actions/setup-dotnet@v4` with
  `dotnet-version: '8.0'`). It is needed to build the Chimera solution and to
  run Chimera's contract tests, not to build the package. Chimera's README
  installs it with
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`.
- The compilers are the system's `gcc` and `g++` (from `build-essential`),
  GCC 13 or later: the engine is built `-std=gnu2x`. The workflow pins no
  compiler version. The guest is compiled by the same `gcc` and `g++` over
  miniBox's guest sysroot (`waterbox/guest.mk`). The gate's hashes are the
  same from GCC 13 (`ubuntu-latest`) and GCC 16.
- What the scripts build or fetch themselves:
  - Mesa 24.0.9's softpipe behind OSMesa, for the OpenGL renderer inside the
    sandbox: `waterbox/setup-mesa.sh` downloads
    `https://archive.mesa3d.org/mesa-24.0.9.tar.xz`, checks its SHA256, and
    builds it for the guest into `build/mesa/` (about 15 minutes). Both builds
    take their GL headers from it and the package carries its licence, so it
    comes first. With no usable meson it makes a Python venv for one.
    `MESA_TARBALL=<path>` uses a tarball already on the machine.
  - SRB2 2.2.15's data, for the gate: `waterbox/fetch-data.sh [<folder>]`
    downloads STJr's own `SRB2-v2215-Full.zip` from github.com/STJr/SRB2's
    releases, takes out the four pk3s and checks each against the SHA-1
    `waterbox.config` declares (default folder `build/srb2-2.2.15`; a folder
    already holding them, checked, is left as it is). CI caches that folder.
    Building the package downloads nothing.

## Get the sources

The workflow checks out both repositories with their submodules, recursively
(`actions/checkout@v6`, `submodules: recursive`), and takes Chimera at `main`.
By hand:

```
git clone --recursive https://github.com/P-AS/chimera-core-srb2.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git -C <core> submodule update --init --recursive
git -C <chimera> submodule update --init --recursive
```

This repository has five submodules (`.gitmodules`): `extern/SRB2` (the
engine: upstream master, net-compatible with 2.2.15), `extern/ogg`,
`extern/vorbis`, `extern/gme` and `extern/openmpt`. `extern/openmpt` is
shallow: only its pinned commit is fetched.

CI puts the Chimera checkout at `<core>/chimera-checkout`. Any place works:
the scripts find miniBox in this order.

1. `-m <miniBox dir>` on `setup-mesa.sh`, `run-gate.sh` and
   `build-package.sh`, or `MB=<dir>` on the `make` command line.
2. The `MINIBOX_DIR` environment variable (what CI sets).
3. For `build-package.sh -r <chimera>` only:
   `<chimera>/extern/chimera-common-minibox`.

There is no default beyond that: export `MINIBOX_DIR=<minibox>` once and
every step below finds it.

## Build Chimera and miniBox

The workflow builds Chimera itself first, then miniBox. Chimera's own build
gives `<chimera>/build/dll/libchimera.so` (used by the gate's engine leg) and
the managed solution (used by the contract tests). The package does not need
it: `build-package.sh` uses only miniBox.

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

Then miniBox: the host, and the guest toolchain with C++ (GME and libopenmpt
are C++, so the guest links miniBox's libstdc++). The guest objects and
libstdc++ are not in miniBox's default ninja target, so they are named.

```
mb=<minibox>
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
ninja -C "$mb/build/meson-linux" source/guest/emulibc.c.o
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
ninja -C "$mb/build/meson-cpp" libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
```

- `build/meson-linux` holds the miniBox host library
  (`source/host/libminiboxhost.so`), which the `run-wbx` harness links.
- `build/meson-cpp` holds the guest sysroot (`guest-sysroot`: musl and
  libstdc++) and the guest objects `guest.mk` links.

CI caches these two build directories (`actions/cache@v4`). By hand there is
nothing to do: the directories stay where they are, and the `[ -f ... ] ||`
guards skip `meson setup` when one is already configured.

## Build the core

```
cd <core>
export MINIBOX_DIR=<minibox>
./waterbox/setup-mesa.sh
make -C waterbox -f native.mk -j"$(nproc)"
make -C waterbox -f guest.mk -j"$(nproc)"
```

The gate does not build them: run these before it.

- **Mesa.** `setup-mesa.sh [-m <miniBox dir>] [-j N]` builds
  `build/mesa/build-guest2/` (the archives and the osmesa target the guest
  links) and `build/mesa/include` (the GL headers). It needs miniBox's
  `meson-cpp` sysroot. A build already there is left as it is (CI caches it).
- **Patches.** `patches/` holds the numbered patches for `extern/SRB2`, and
  `patches/openmpt/` the one for `extern/openmpt`. Both makefiles apply them
  first, through `waterbox/apply-patches.sh` and
  `waterbox/apply-patches.sh openmpt` (`build/patches.stamp` and
  `build/patches-openmpt.stamp` record that they ran). The script judges a
  series as a whole. A pristine submodule gets every patch. A tree that
  already carries the whole series is left alone. Anything in between is an
  error that names the files and prints the command to start again. The
  series is first tried on a scratch copy, so a series that does not apply
  never touches the tree. `SRB2_TREE` (`PATCH_TREE` for a library) points it
  at another checkout.
- **The guest core** (`guest.mk`): `build/guest/core.wbx`. It is SRB2, zlib
  and libpng from SRB2's `libs/`, libogg, libvorbis, GME, libopenmpt, Mesa
  and the core's own sources, built with the system compilers over miniBox's
  guest sysroot. miniBox's `check-wbx.sh` checks the result (no thread-local
  storage, no `%fs`, no red zone) before it counts as built.
- **The native reference** (`native.mk`): `build/native/run-native` and
  `build/native/run-wbx`. `run-native` is the same sources built for the
  host with no sandbox (and no GL: its OpenGL renderer falls back to
  software, and says so). `run-wbx` drives `core.wbx` through the miniBox
  host as the frontend does. The gate compares the two step by step. Neither
  is part of the package. Their use is in `README.md` ("Running the native
  reference").

`sources.mk` is included by both makefiles, so both build the same files with
the same defines; its source lists are upstream's own `Sourcefile`s. Each
object depends on the flags it was built with: a change of flags rebuilds it.
`make -C waterbox -f guest.mk clean` and `make -C waterbox -f native.mk clean`
remove `build/guest` and `build/native`.

`waterbox/waterbox.config` (the declaration: system, firmware, settings,
controller), `default_keybinds.json` and `file_slots.json` are written by
hand. `build-package.sh` refuses a package whose keybinds and declared
buttons disagree, or whose system name, button letters or axis headers are
missing; the gate's `declaration` leg checks the declared controller against
the core's own.

## Build the package

```
cd <core>
./waterbox/build-package.sh -r <chimera>
```

`waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root or bundle>] [-o <out dir>]`:

- `-r <chimera root>` writes `<chimera root>/build/Cores/srb2.chimeraCore` and
  takes miniBox from that checkout unless `-m` or `MINIBOX_DIR` names another.
  It also removes `<chimera root>/build/CoreCache/srb2-*`. This is what CI runs
  (`-r chimera-checkout`).
- `-r <Chimera bundle>` (a folder with `Chimera.exe`) writes
  `<bundle>/Cores/srb2.chimeraCore`, and clears `<bundle>/CoreCache/srb2-*`.
  A bundle has no miniBox: name it with `-m` or `MINIBOX_DIR`.
- `-o <out dir>` writes `<out dir>/srb2.chimeraCore` instead.
- With neither, the package is `<core>/build/package/srb2.chimeraCore`.

The script builds the guest (`guest.mk`; the log is `build/package-make.log`),
checks `core.wbx` and the declaration, and packs `core.wbx`,
`waterbox.config`, `default_keybinds.json`, `file_slots.json`, the licence
texts (`waterbox/package-licenses.json`, through miniBox's
`package-licenses.py`) and a `build.json` that records what built the
package. The packing is deterministic: the script packs twice and stops if
the two SHA-1s differ, then prints `package sha1 <hash>`.

The version is the commit the package was built from, and its date the
commit's (UTC), never the build's.

- CI sets `CORE_VERSION` to the commit's full hash.
- By hand, with `CORE_VERSION` unset, the script stamps `<commit>+local`
  (twelve hex digits), or `<commit>-dirty+local` when the tree has changes.
  The patches applied inside `extern/` do not count as changes.

A hand-built package is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

CI publishes what its gate passed as this repository's own releases: a rolling
`dev` release on every green push to `main`, and a dated `nightly-YYYY-MM-DD`
release from the scheduled run (04:23 UTC, only when `main` moved since the
last one; Actions > Run workflow with kind `nightly` publishes one by hand).
The gate job uploads the package as the artifact `srb2-<commit>`
(`actions/upload-artifact@v7`), and the publish job hands it to Chimera's
public reusable workflow `publish-core.yml`. There is no manual equivalent.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody put its package file in the cores folder.
This core is not in the list of cores Chimera's README links to; that is the
Chimera project's to decide, and nothing here depends on it.

- **A release bundle of Chimera**: download `srb2.chimeraCore` from this
  repository's Releases page
  (https://github.com/P-AS/chimera-core-srb2/releases), or build it, and put
  it in the `Cores` folder beside `Chimera.exe`. Another folder can be chosen
  in File > Core Manager > Change folder...
  `./waterbox/build-package.sh -r <bundle>` writes it there.
- **A Chimera source checkout**: the cores folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight there.

File > Core Manager lists what is in the folder. Refresh List rescans it. The
same package file works on Linux and on Windows: the guest inside it is run by
Chimera's sandbox (miniBox) on either.

## Run the gates

### The core gate

```
cd <core>
./waterbox/fetch-data.sh build/srb2-2.2.15
MINIBOX_DIR=<minibox> ./waterbox/run-gate.sh -d build/srb2-2.2.15 -c <chimera>/build
```

This is the command CI runs (after `fetch-data.sh` and both builds). The gate
works in `build/gate` (the data linked in, never copied), prints `PASS` or
`FAIL` for every leg (and `SKIP` for what the machine cannot do), and ends
with `gate: all legs pass`, or `gate: FAILED` and a non-zero exit.

It proves that the native reference and the sandboxed core are the same
machine, that the machine is deterministic and its savestates exact, and that
Chimera's own engine runs the package. The content is the game's start (the
intro) and Greenflower Zone Act 1, the movies in `waterbox/tests/`, and two
PWADs the gate makes (a GME song and a tracker module). The legs, as the
script's header lists them, each with its teeth: equivalence, steps,
savestates, input, properties, audio, files, exports, declaration, settings,
resolution, opengl, engine and time.

Usage: `run-gate.sh [-d <SRB2 data folder>] [-m <miniBox dir>] [-c <Chimera bundle or build/>]`

- `-d <dir>` names a folder with 2.2.15's srb2.pk3, zones.pk3,
  characters.pk3 and music.pk3. Default `/usr/share/games/SRB2`.
- `-c <dir>` runs the engine leg: a Chimera checkout's installed `build/`
  (`meson install --libdir dll`) or a bundle. The gate packages the core into
  `build/gate/package` and opens it through `<dir>/dll/libchimera.so`, as the
  frontend's session does, and runs the menus into a new game. It also runs
  `opengl-hw`: with no GPU bridge offered it must be the Mesa's run; where the
  machine gives the engine a GL context, through the bridge, the same game,
  and the picture right again after a rewind and a reopen. With no context
  that part is a `SKIP` line. Without `-c` the leg does not run.

### Chimera's contract tests

Run after the package is in `<chimera>/build/Cores`:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They run Chimera's own checks against this package: it is readable, it is
built for an ABI this frontend runs, it makes a working factory, it binds only
buttons its controller declares, it stamps a version, and the letters its
buttons declare are all there, writable and distinct. They need the Chimera
solution built and no game files.

## Files the core needs at run time

The package carries none of the game's data. A project brings SRB2 2.2.15's
four pk3s as firmware; `waterbox/waterbox.config` declares them with their
SHA-1s:

| Firmware | What it is |
| --- | --- |
| `srb2.pk3` | The game's graphics, sounds, objects and scripts |
| `zones.pk3` | The game's levels |
| `characters.pk3` | Sonic, Tails, Knuckles, Amy, Fang and Metal Sonic |
| `music.pk3` | The game's music |

- They are 2.2.15's, as STJr's release and the usual installs have them
  (`/usr/share/games/SRB2` on many Linux systems). The engine itself refuses
  srb2.pk3, zones.pk3 and characters.pk3 whose MD5 is not 2.2.15's.
- A project's own files (`waterbox/file_slots.json`) are optional: Add-ons
  (`.pk3`, `.wad`, `.soc`, `.lua`, in load order, as SRB2's `-file` loads
  them) and Save data (`gamedata.dat` and `srb2sav*.ssg`, as Export Save Data
  writes them).
- No BIOS and no other firmware is needed.

## Troubleshooting

- `name miniBox: -m <path> or MINIBOX_DIR (built with -Dguest_cpp=true)`
  (`build-package.sh`), `name miniBox: make -f guest.mk MINIBOX_DIR=<miniBox checkout>`
  (the makefiles): no miniBox was named. Export `MINIBOX_DIR`, or pass `-m`
  or `-r <chimera checkout>`.
- `miniBox's C++ guest toolchain is missing: ...` (`guest.mk`) or
  `guest sysroot not built at ...` (`setup-mesa.sh`): miniBox was built
  without `-Dguest_cpp=true`, or its guest targets were not named. Build
  `build/meson-cpp` as in "Build Chimera and miniBox".
- `no Mesa at build/mesa: run waterbox/setup-mesa.sh first` or
  `no guest Mesa at ...` (the makefiles): run `./waterbox/setup-mesa.sh`.
- `mesa: no usable meson - install python3-venv, or meson plus python3-mako`
  (`setup-mesa.sh`): install what it names.
- `extern/SRB2 is not checked out` (`apply-patches.sh`): the clone was made
  without its submodules. Run the `git submodule update` command it prints.
- `extern/SRB2 is partly patched` (`apply-patches.sh`): the submodule's tree
  is neither pristine nor what the whole series leaves. The script prints the
  way back:
  `git -C extern/SRB2 reset --hard && git -C extern/SRB2 clean -fd && waterbox/apply-patches.sh`.
  That discards edits made in the tree: turn them into a patch first. The same
  holds for `extern/openmpt` with `waterbox/apply-patches.sh openmpt`.
- `the series does not apply to the submodule's HEAD at <patch>`
  (`apply-patches.sh`): the submodule was moved without rebasing the patches.
- `build the native reference first` or `build the core first` (the gate):
  the gate does not build them. Run the two `make` commands.
- `no data folder at ...` or `no <file> in <dir>` (the gate): `-d` names no
  folder with the four pk3s. Run `./waterbox/fetch-data.sh` and pass its
  folder.
- `<file>: sha1 ..., declared ...` (`fetch-data.sh`): the download is not
  2.2.15's data. Run it again: it downloads again and checks again.
- `the guest build failed (build/package-make.log)` (`build-package.sh`): read
  that log.
- `default_keybinds.json does not match the declared buttons`, or a message
  about `mnemonics`, `systemNames` or axis headers (`build-package.sh`): the
  declaration and the keybinds were changed apart. Change them together.
- `git status` shows `extern/SRB2` or `extern/openmpt` as modified after a
  build: the patches are applied in the submodules' working trees. That is
  expected, and it does not mark the package `-dirty`.
