# chimera-core-srb2

A third-party [Chimera](https://github.com/ToolAssisted-run/chimera) game core for [Sonic Robo Blast 2](https://srb2.org/) v2.2.15. This core facilitates tool-assisted speedrunning of Sonic Robo Blast 2 (including mods) with the benefits of the Chimera TAS frontend, such as a simplified setup process, cross-platform support, an easily accessible game state with a lua-based TAS info overlay, the TAStudio input editor, and persistent savestates.

> [!IMPORTANT]
> chimera-core-srb2 was primarily written using AI assistance from Anthropic's Claude LLMs. [libTAS](https://github.com/clementgallet/libTAS) is a viable alternative to this core.

## Usage

chimera-core-srb2 supports Windows, macOS, and Linux, on both the x86_64 and aarch64 architectures. To use it, download the latest Chimera core package from the [Releases](https://github.com/P-AS/chimera-core-srb2/releases) page for your CPU's architecture. Place the file in Chimera's `Cores` directory. You can access this folder from Chimera by clicking Tools --> Core Manager... --> Open cores folder.

If you are running Windows or Linux, you most likely want to download the x86_64 Chimera core package, while on macOS you most likely want to download the aarch64 Chimera core package.

A copy of Sonic Robo Blast 2 v2.2.15's srb2.pk3, zones.pk3, characters.pk3 and music.pk3 asset files are required to be loaded as firmware for the core to function.

The bundled TAS info overlay lua script can be loaded in Chimera from Tools --> Lua Console --> Script --> Open Script.

## Building

Building is for Linux (CI uses Ubuntu). The core is built with miniBox, the sandbox Chimera runs cores in, from a
Chimera checkout (`git clone --recursive https://github.com/ToolAssisted-run/chimera.git`). Build miniBox first,
with its C++ guest toolchain (GME and libopenmpt are C++):

```
mb=<a Chimera checkout>/extern/chimera-common-minibox
meson setup "$mb/build/meson-linux" "$mb" && meson compile -C "$mb/build/meson-linux"
ninja -C "$mb/build/meson-linux" source/guest/emulibc.c.o
meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true && meson compile -C "$mb/build/meson-cpp"
ninja -C "$mb/build/meson-cpp" libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
```

Then the core:

```
git submodule update --init --recursive
export MINIBOX_DIR=<a Chimera checkout>/extern/chimera-common-minibox
make -C waterbox -f native.mk -j$(nproc)    # the native test programs: build/native/
make -C waterbox -f guest.mk -j$(nproc)     # the core: build/guest/core.wbx
```

The makefiles apply the patches in `patches/` to `extern/SRB2` and `extern/openmpt` on the first build.

### Packaging

```
waterbox/build-package.sh [-o <folder>]
```

This builds the core if needed and writes the package, `srb2.chimeraCore`, into `<folder>`, or into
`build/package/` without `-o`. Put that file in Chimera's `Cores` folder to use it.

## Settings

Chimera shows these as the core's options (they are declared in `waterbox/waterbox.config`). Each is one of SRB2's
own options, given to the game when it starts.

| Setting | Default | Choices and notes |
|---|---|---|
| Play Style | Manual | Strafe, Manual, Automatic, Old Analog |
| Camera Speed | 1.0 | |
| Camera Distance | 192 | |
| Camera Height | 40 | |
| Field of View | 90 | 60 to 179. Changes only the picture. Leave it at 90 for normal levels; 120 is recommended for NiGHTS levels |
| Score/Time/Rings | Mania | Classic, Centiseconds, Mania, Tics |
| Flip Camera with Gravity | Yes | |
| Automatic Braking | On | |
| Music Preference | Digital | Digital: the recorded songs. MIDI: the game's MIDI songs on six emulated Sega Genesis/Mega Drive YM2612 sound chips (MAME's), as upstream's Windows builds play them. Changes only the sound |
| Tutorial Prompt | Off | |
| Unlock Record Attack/NiGHTS Mode/Marathon Run | Off | |
| Unlock All Characters | Off | |
| Unlock All Maps in Record Attack and NiGHTS Mode | Off | |
| Unlock All Secrets | Off | |
| Resolution | 1280x800 | SRB2's video modes, plus 2560x1440 and 3840x2160, shown with square pixels. The game plays the same at every one |
| Renderer | software | software, or opengl-hw: SRB2's OpenGL renderer on this computer's graphics card through Chimera's GPU bridge. opengl-hw is fast, but its picture depends on the graphics driver. Where Chimera has no GPU bridge it draws in software, which plays differently, and the log warns. The renderer is part of the game, so a movie must be played back with the renderer it was made with |
| OpenGL Shaders | On | Changes only the picture |
| Start Map | (empty) | A map number to start in. Empty: the intro and the title screen |
| Start Map Character | (empty) | A character's skin name, for example knuckles. Empty: Sonic |

## Inputs

The buttons are SRB2's default keyboard keys, pressed as keys, so the menus and play work as on a keyboard. Enter and
Escape are the menus' keys, and also answer a prompt's yes and no. The ring-slinger controls are included, but not
the number row's weapon slots: Weapon Next and Weapon Prev reach every weapon.

### Digital

| Input | TAStudio header |
|---|---|
| Forward | `^` |
| Backward | `v` |
| Strafe Left | `<` |
| Strafe Right | `>` |
| Turn Left | `{` |
| Turn Right | `}` |
| Look Up | `U` |
| Look Down | `D` |
| Jump | `J` |
| Spin | `S` |
| Fire | `F` |
| Fire Normal | `N` |
| Toss Flag | `G` |
| Center View | `C` |
| Reset Camera | `R` |
| Camera Toggle | `T` |
| Weapon Next | `+` |
| Weapon Prev | `-` |
| Custom 1 | `1` |
| Custom 2 | `2` |
| Custom 3 | `3` |
| Pause | `P` |
| Enter | `E` |
| Escape | `e` |

### Analog

The axes set exact values. Forward Move and Side Move (-50 to 50) count only while their buttons are not held. Turn
and Aim range from -32768 to 32767; Aim at 0 leaves the game's own look.

| Input | TAStudio header |
|---|---|
| Forward Move | Fwd |
| Side Move | Side |
| Turn | Turn |
| Aim | Aim |

## Game State and the TAS info overlay

### Game State

The core has one memory area, `Game State`, with a table naming its fields, so RAM Watch (Watches > Add Game
Properties), RAM Search and Lua's `game.get` see them by name. The core copies them from the game after every step,
and they are read-only. Positions, floor and ceiling heights, momentum, speed and the spindash values are 16.16 fixed point: 65536 is one unit.

| Property | What it is |
|---|---|
| `Game.Tic` | The game's tic counter (a tic is a 35th of a second) |
| `Game.Level Time` | Tics since the level started |
| `Game.State` | SRB2's game state (level, intermission, title screen and so on) |
| `Game.Map` | The current map number |
| `Player.In Level` | Whether the player is in a level. Out of a level the player's fields are 0 |
| `Player.X / Y / Z` | Position |
| `Player.Floor Z / Ceiling Z` | The height of the floor below the player and the ceiling above them (SRB2's `floorz`/`ceilingz`) |
| `Player.Momentum X / Y / Z` | Momentum |
| `Player.Conveyor Momentum X / Y` | What a conveyor or a moving platform adds to the momentum (SRB2's `cmomx`/`cmomy`) |
| `Player.Platform Momentum Z` | The moving floor's vertical momentum (SRB2's `pmomz`) |
| `Player.Speed` | Speed, the game's own `player->speed` |
| `Player.Angle` | The direction the player faces: 2^32 is one full turn |
| `Player.Dash Speed` | The spindash's charge |
| `Player.Min Dash / Max Dash` | The spindash's lowest and highest charge |
| `Player.Flags` | The player's flags (SRB2's `pflags`) |
| `Player.Charging Spindash` | Whether a spindash is charging (the `PF_STARTDASH` flag) |
| `Player.Dashmode` | Metal Sonic's dash mode counter |
| `Player.Normal Speed` | The top speed, which dash mode raises |
| `Boss.Active` | Whether there is a boss: the first object with `MF_BOSS` |
| `Boss.Health` | The boss's health |
| `Boss.Max Health` | Its type's starting health |
| `Boss.Flashing` | Whether it is flashing after a hit (`MF2_FRET`) |
| `Timers.Speed Shoes` | Tics left of speed shoes |
| `Timers.Invincibility` | Tics left of invincibility |
| `Timers.Space` | Tics left before drowning in space |
| `Timers.Air` | Tics left before drowning underwater |

### TAS info overlay

`lua/tasinfo.lua` (also attached to every `dev` and nightly release, beside the package) draws these values in the
bottom right corner of the game, clear of Chimera's own display. Open it in Tools > Lua Console with an SRB2
project loaded.

| Shows | Notes |
|---|---|
| Speed |  |
| Angle | In hex and in degrees (to 4 decimal places), each on its own line |
| Position and momentum | Position includes the floor and ceiling heights, under Z |
| Conveyor and platform momentum | Only while not 0 |
| Spindash revs | Only while charging. SRB2 has no separate revs: a spindash charges 1.0 of speed a tic while Spin is held, from Min Dash to Max Dash (Sonic: 15 to 70), so the overlay counts a rev a tic, as revs so far / revs to full charge |
| Metal Sonic's dash mode | Only while not 0, and its speed at 108 |
| The four timers | In tics and seconds, only while not 0 |
| Boss health and flashing | While a boss is in the level: health left / health at the start |

## Licence

GPL-2.0, as SRB2 is (`LICENSE`). zlib and libpng are compiled from the copies in SRB2's `libs/`, under their
own licences; libco (miniBox's `extern/libco`) is public domain; libogg and libvorbis (`extern/ogg`,
`extern/vorbis`) are BSD-3-Clause; Game_Music_Emu (`extern/gme`) is LGPL-2.1-or-later (its Nuked YM2612 too); libopenmpt (`extern/openmpt`) is
BSD-3-Clause. The MIDI player (`waterbox/platform/midi_fm.cpp`) is the core's own, GPL-2.0, because libOPNMIDI's
player is GPL-3.0-or-later and cannot be part of a GPL-2.0 program; it is built on the parts of libOPNMIDI
(`extern/opnmidi`) whose terms allow it: its MIDI sequencer, WOPN bank loader, XG bank and volume and frequency
models (MIT), its chip interface (LGPL-2.1-or-later) and MAME's YM2612 (GPL-2.0-or-later). Sonic the Hedgehog and related characters are trademarks of SEGA; this repository carries
none of the game's data.
