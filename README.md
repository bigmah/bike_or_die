# Bike or Die 2 — native on Apple Silicon

Bike or Die 2 (ToySpring, 2008) was a Palm OS 5 game. This repository turns it into a
native macOS/arm64 application by **statically recompiling the game's own machine code**
— both instruction sets it ships — and running it against a native re-implementation of
the Palm OS API. No Palm ROM and no CPU interpreter are used for the game code.

    open "build/Bike or Die 2.app"

## What the game actually is

`files/BikeOrDie-2.1b.prc` is a Palm OS 5 application, creator `BiKD`, 284 resources:

| resource | size | what it is |
|---|---|---|
| `code` 0-3 | 81 KB | Motorola 68000 code — UI, menus, level/database logic |
| `armc` 1-4 | 199 KB | native **ARM32** code — physics and the span/texture renderer |
| `data`/`rloc` | 1.5 KB | compressed A5 world (globals) + relocation chains |
| `Tbmp`,`BikL`,`tex0`,`Pwav`,`trck`,`tFRM`… | 1 MB | sprites, levels, textures, sound, forms |

It calls **245 distinct Palm OS traps** from 1,493 call sites, including 34
`PceNativeCall`s into the ARM blobs. An M4 cannot execute ARM32 at all, so that half had
to be translated too, not just run.

## How it is built

1. **Palm OS API, natively.** [PumpkinOS](https://github.com/migueletto/PumpkinOS) (GPLv3)
   re-implements the Palm OS API for modern machines. It had no macOS support; this repo
   adds a Darwin/arm64 port (`vendor/PumpkinOS`, see `docs/PORTING.md`).
2. **Static recompilation.** `tools/recomp68k.py` and `tools/recomparm.py` translate the
   game's code to C, which clang compiles to arm64. Every aligned offset in every code
   resource is translated and the generated chunk indexes a label array, so jump tables
   and computed branches need no control-flow recovery:

   | | reachable instructions | translated |
   |---|---|---|
   | 68000 | 23,337 | 23,330 (99.97%; the 7 misses are data bytes) |
   | ARM32 | 23,877 | 23,877 (100%) |

   That becomes ~185k lines of generated C (`src/gen`), ~11 MB of native code.
3. **Bridging.** Traps become direct calls into the Palm OS layer; `PceNativeCall`,
   the ARM→68k trampoline and the PACE syscall addresses are handled by the glue in
   `emupalmos.c`.

## Controls

| key | action |
|---|---|
| **↑** | Forward (pedal) |
| **↓** | Brake |
| **←** / **→** | Balance left / right |
| **Space** | **Flip** -- turn around and ride the other way |
| F1-F4 | the four Palm hardware buttons |
| F5 | Palm menu (Game / Rec / Options / Help) |

Those are the game's own defaults, visible and rebindable under **Options → Control**
(F5, then Options, then Control). Its five actions map to a Palm 5-way navigator:
Forward=Up, Brake=Down, Balance=Left/Right, Flip=Select.

A Palm OS 5 device has no left/right/select in the base key manager, so games poll a
vendor navigator mask instead. Bike or Die keeps its table of available physical keys at
`globals+0x3E74` and expects `keyBitNavLeft`/`NavRight`/`NavSelect`
(0x01000000/0x02/0x04). PumpkinOS only reported its own 0x400/0x800 and nothing at all
for select, so the arrows did nothing and Flip was unreachable; it now reports the
palmOne navigator and Handspring rocker bits, with space and F9 as select.
(`BOD_DUMP_KEYS=1` dumps that table if another game needs the same treatment.)

## Configuration

The launcher reads `~/.bikeordie.env` if it exists. Useful settings:

| variable | default | meaning |
|---|---|---|
| `PUMPKIN_USER` | `PalmDB` | HotSync user name; the registration code is keyed to it |
| `PUMPKIN_DISPLAY_LE` | `1` | the ARM blitter writes little-endian RGB565 |
| `PUMPKIN_SOUND` | `1` | enable audio (sets both the global and per-app switch) |
| `PUMPKIN_VOLUME` | `64` | Palm sound volumes, 0-64 |
| `BOD_RECOMP` | `1` | use the statically recompiled cores |
| `BOD_ARM_ENGINE` | `recomp` | `interp` falls back to the ARM interpreter |
| `BOD_RESET` | `0` | `1` re-seeds the game data on next launch |
| `BOD_ZOOM` | `3` | integer window scale; the game itself is 320x320 |

Game data (saves, level packs, preferences) lives in
`~/Library/Application Support/Bike or Die 2`.

## Status

Both cores are used by default. The game boots, renders and plays entirely on
statically recompiled code; the ARM interpreter is only kept around as a reference for
differential testing.

## Sound

Three separate things default to silent, and all three have to be on:

- PumpkinOS's **global** sound switch (`prefs.value[pEnableSound]`, default 0),
- the **per-app** registry flag (`regSoundID`, default 0),
- the Palm **volume** preferences (`sysSoundVolume`/`gameSoundVolume`/`alarmSoundVolume`,
  all default 0) -- and Bike or Die's own Sound setting is "Automatic", which means it
  follows the system *game* volume.

`PUMPKIN_SOUND` now sets the first two and `PUMPKIN_VOLUME` the third, both applied after
the stored preferences load, so an existing preferences database does not need resetting.
The game drives audio as an ARM-native `SndStreamCreate` callback at 44.1 kHz mono 16-bit.

## Display

The Palm screen is 320x320 and the window is that, scaled by an integer factor
(`BOD_ZOOM`, default 3 -> a 960x960 window). PumpkinOS's desktop is sized to match, so
there is no surrounding chrome to redraw.

The bundle sets `NSHighResolutionCapable` to **false** on purpose. With a Retina backing
store the SDL drawable is twice the window size, which PumpkinOS's `xfactor` scaling does
not account for, and the screen comes out magnified and clipped -- but only when launched
as a bundle (via Finder or `open`), because running the binary directly never reads the
Info.plist. With a 1x backing the integer scaling is exact.

## Testing

`tools/headless.sh` runs the game with no window at all, using the `libwtest` display
driver: it keeps the framebuffer in memory, writes PPM snapshots, and takes input from a
plain-text script (`tools/scripts/*.txt`). That makes it possible to drive and compare
builds without synthesising system-wide mouse and keyboard events.

    tools/headless.sh tools/scripts/ride.txt 32 build/frames

Useful knobs while debugging the ARM core:

| variable | meaning |
|---|---|
| `BOD_ARM_ENGINE=interp` | run all `armc` code under the interpreter |
| `BOD_ARM_BLOBS=<mask>` | bitmask of `armc` blobs to run recompiled (bit 0 = armc 1) |
| `BOD_ARM_IBLOB`/`ILO`/`IHI` | force one offset range of one blob to the interpreter |
| `BOD_ARM_BATCH=1` | exact range semantics (no overshoot) -- required for bisection |
| `BOD_ARM_RING=1` | dump recent ARM state to `/tmp/bod_ring.txt` on a wild access |
| `BOD_ARM_STRICT=1` | make out-of-range accesses fatal instead of clamped |

`tools/bikecheck.py` scores a frame for how much of the (red) bike is visible, which is
what makes the bisection automatic.

## Licensing

PumpkinOS is GPLv3; this port and the recompiler tooling inherit that. The game itself is
not redistributable — `files/` holds the user's own copy.
