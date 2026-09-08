# Bike or Die 2 — native macOS (Apple Silicon) port

## Input
`files/BikeOrDie-2.1b.prc` — Palm OS 5 application, creator `BiKD`, 284 resources:
- `code` 0..3  — 81 KB of Motorola 68k code (UI, game shell, level/DB logic)
- `armc` 1..4  — 199 KB of native **ARM32** code (physics + rendering fast paths),
                 each prefixed by a u32 table of function entry offsets
- `data` 0 / `rloc` 0 — compressed A5 world (globals) + relocations
- assets: `Tbmp` sprites, `BikL` levels, `tex0` textures, `Pwav` sounds, `tFRM` forms,
  `trck` soundtrack, `BiSP`/`BiSk` bike/skin data
Uses **245 distinct Palm OS traps** (1493 call sites) incl. 34 `PceNativeCall`.
`0xA8xx` traps are NetLib (online leaderboard) — stubbable.

## Target
Native arm64 macOS binary. M4 cannot execute ARM32 at all, so the `armc` code must be
translated, not run.

## Approach
1. **PalmOS API layer** — port `PumpkinOS` (GPLv3 native re-implementation of Palm OS,
   165 kLOC) to macOS/arm64. Gives Form/List/Control/Field/Window/Bitmap/Font/Dm/Mem/Snd.
2. **Reference run** — get the game running under PumpkinOS's *interpreters*
   (Musashi 68k + uARM-derived PXA core) to have a known-good oracle.
3. **Static recompilation** — translate `code`1-3 (68k) and `armc`1-4 (ARM32) ahead of
   time into C, compiled by clang to native arm64. Emitted as one C function per code
   resource using a label array + computed goto, so indirect branches and jump tables
   stay correct. Traps become direct calls into the PalmOS layer.
4. **Native shell** — SDL window, keyboard controls, audio.

## Status — complete

- [x] PumpkinOS ported to macOS/arm64 (see `docs/PORTING.md`)
- [x] 68k static recompiler — 23,330 / 23,337 reachable instructions (99.97%)
- [x] ARM32 static recompiler — 23,877 / 23,877 reachable instructions (100%)
- [x] Both cores are the default; the interpreter is kept only as a test reference
- [x] Three ARM translation bugs found and fixed (see `docs/RECOMPILER.md`);
      lockstep verification against the interpreter now reports zero mismatches
- [x] Headless, scriptable test harness (`tools/headless.sh`, `libwtest`)
- [x] Display: 320x320 at 3x, no desktop chrome
- [x] 210 level packs installed
- [x] Keyboard controls verified end to end
- [x] Packaged as `build/Bike or Die 2.app`

See `README.md` to run it, `docs/RECOMPILER.md` for how the translation works and how the
bugs were found.
