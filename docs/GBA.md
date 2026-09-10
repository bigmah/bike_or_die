# Bike or Die 2 on a Game Boy Advance

`make -C gba` builds `build/gba/bod.gba`, a 12 MB ROM that runs the game on the GBA's
own ARM7TDMI. The 68000 half is the same statically recompiled C as the desktop build;
the ARM half is the game's original `armc` code, executed natively, patched at ROM build
time to fit a machine with 256 KB of RAM and a 16 MHz CPU. Play it with mGBA (`mgba
build/gba/bod.gba`), or add the ROM to the `wacky_stackers` web player, which keeps it
in the browser; the headless test harness is `tools/gba/run.sh`.

## Controls

| GBA | game |
|---|---|
| D-pad | lean / accelerate / brake, as the arrow keys |
| A | select (five-way centre) |
| B | escape / back |
| Start | menu |
| Select | switch between the fast renderer (default) and the sharp one, from the next level |
| L, R | hard keys 2, 3 |

Progress and settings are saved to the cartridge's 32 KB SRAM (mGBA keeps it in a
`.sav` file next to the ROM). The first boot seeds the game's settings from
`tools/gba/bikd-prefs-3.bin`, a preference record saved by a Palm with textures and the
background layer on.

## What had to change

A Palm OS 5 device gave this game a 4 MB dynamic heap and a 144-400 MHz CPU. The GBA
has 256 KB of work RAM and 32 KB of fast RAM. Three things made it fit and run:

**Textures come from ROM.** The engine tints every texture at level load: it doubles the
tex0 source, maps grey through the level's colour gradient (with error diffusion on
8-bit screens) and builds a mipmap pyramid, half a megabyte per level. On the GBA the
grey images live in ROM (`tools/gba/pretex.py` precomputes the plain and smoothed
doublings and the pyramids for all 16 textures) and the sampler tints texels through a
256-entry table per texture slot at render time, in IWRAM. Error diffusion becomes a
two-table checkerboard dither. The loader's own doubling, colourising and mipmap
generation return early when the buffer they were handed is in ROM
(`tools/gba/patches/armc3.s`); the runtime answers the loader's allocation requests with
the ROM addresses by recognising the call site (`gba/src/texhook.c`).

**The scanline edge table is packed.** The renderer's per-band list of edge crossings
was sized for a fixed capacity per row and regrown by 1.5x until nothing overflowed
(184 KB for the tutorial). It is now built in two passes, the first only counting
(`gba/src/edge.c`, the patched insert routine), into rows of exactly the size they need.
A level whose table would still crowd out the rest of the load gets coarser bands.

**The scene is drawn at half size.** By default the engine renders into a 120x80
window and the copy to the screen doubles every pixel, which quarters the sampling,
line and sprite work; the menus, dialogs and text stay at full resolution. Select
switches to full-size rendering, which looks like the original and runs at 12-14
frames per second instead of 21-22.

**Hot code runs from IWRAM.** Executing the engine from the cartridge is slow whenever it
reads ROM data (the prefetch restarts after every texel). `tools/gba/hotmove.py` moves
whole functions into IWRAM at ROM build time, rewriting branches, out-of-range calls
(through trampolines) and pc-relative address arithmetic, and redirects every original
entry point. The texture samplers were rewritten by hand for the same reason
(`tools/gba/patches/armc2.s`).

Around those, the Palm OS layer (PumpkinOS) was trimmed and sped up for the GBA: forms
and menus draw straight into VRAM instead of into buffers that get copied, the bitmap
header accessors are cached, rectangle fills and lines have whole-run fast paths, the
display palette follows the colour table the engine edits, and the memory manager keeps
resources in ROM behind the same chunk headers it uses for RAM (`gba/src/storage.c`,
`tools/gba/packdata.py`).

## Numbers

| | Palm (native build) | GBA |
|---|---|---|
| level load (tutorial) | under a second | about 7 s |
| frame rate | 30+ | 21-22 half size, 12-14 full size |
| RAM at play | ~600 KB | ~180 KB of 256 KB |
| textures | error-diffused | checkerboard dithered |
| edge bands (tutorial) | 8 units | 64 units |
| sound | yes | no |

## Layout

    gba/            crt0, linker script, Makefile, the runtime (heap, storage, hooks)
    gba/shim/       headers that stand in for PumpkinOS's host layer
    gba/pk/         trap dispatchers copied from PumpkinOS (tools/gba/sync_pumpkin.sh)
    tools/gba/      packdata.py (ROM image), armpatch.py + patches/ (engine patches),
                    hotmove.py (IWRAM relocation), pretex.py (grey textures),
                    gbarun.c + run.sh (mGBA test harness)

`tools/gba/run.sh [-x] <script> [seconds] [outdir]` runs a key script headlessly and
leaves screenshots and `run.log` in the output directory; `-x` samples the program
counter. Scripts are lines of `wait ms`, `key name`, `keydown`/`keyup`, `shot label`
and `dump addr len file`. The ROM logs through mGBA's debug port; each five seconds it
prints engine calls, milliseconds spent in the engine, and the heap state.

## Known limits

- No sound: the sound streams are refused. The engine's mixer would need a fifth of
  the CPU the renderer already lacks.
- Big levels render their edges at coarser vertical bands (see above).
- Textures blended from two sources (the tutorial's ground) show only the first.
- The game thinks it has a 4 MB heap (`MemHeapFreeBytes` says so), which keeps its
  background layer on; a level that really needs more than the GBA has fails to load
  its table or textures and plays without them.
- Bus timings ask for 2/1 ROM wait states and 1-wait EWRAM, which mGBA honours; a
  cartridge would need 3/1.
