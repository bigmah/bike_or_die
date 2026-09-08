# The recompilers

`tools/recomp68k.py` (68000) and `tools/recomparm.py` (ARM32) are the same design.

## Translate every slot

Both translate **every aligned offset** in every code resource — every even byte for
68k, every 4 bytes for ARM — and emit one C function per chunk containing a label per
slot plus a label array:

```c
void seg1_c0(r68k_state *S) {
  REGS(); LOAD();
  static void *const L[] = { &&I_000000, &&I_000002, ... };
  goto *L[(S->pc - SEGBASE) >> 1];
 I_000000: { ... }
 I_000002: { ... }
```

This is what makes the whole thing tractable: an indirect branch is
`goto *L[(target - SEGBASE) >> 1]`, so jump tables, function pointers, `rts`, `bx lr` and
the ARM `add pc, pc, rN, lsl #2` dispatchers all work with **no control-flow recovery at
all**. Bytes that are really data translate to code that is never reached; anything that
fails to decode becomes a diagnostic, not a wrong answer.

Two consequences worth knowing:

- Fall-through must be an explicit `goto I_{addr+size}` whenever the instruction is longer
  than one slot — the textually adjacent label is the *middle* of the current instruction.
- Branch targets that are misaligned only ever come from decoding data, so they emit a
  diagnostic rather than a label reference.

## Registers and state

The register file lives in C locals inside each chunk function and is written back to the
state struct only at chunk exits and around traps, so clang keeps it in machine registers.
Cross-chunk control flow returns to a small dispatch loop (`r68k_run` / `rarm_run`).

## Decoding

Capstone provides the decode. Two of its quirks matter:

- **M68K**: the base register of `(An)`, `(An)+` and `-(An)` is in `op.reg`, not
  `op.mem.base_reg` (which is only filled for the displacement forms). Absolute addresses
  are not exposed at all and are parsed out of `op_str`.
- **ARM**: post-indexed addressing is signalled by `writeback` plus a *third* operand
  holding the increment; pre-indexed puts the displacement in the memory operand.

## Bugs this design made easy to find

Because the interpreter is still present, the recompiled ARM can be diffed against it
instruction by instruction (`BOD_ARM_ENGINE`, `tools/diff_trace.py`). Both real bugs were
flag bugs:

1. Non-`S` ARM arithmetic was writing C and V, because the add/subtract helper always set
   them. Control flow derailed a few thousand instructions later.
2. `CMP`/`CMN` were taking C from the **barrel shifter** instead of from the subtraction.
   Only the logical tests (`TST`/`TEQ`) and the move/logical forms do that. This broke the
   software divide routine (`cmp r0, r1, lsr #1`), which is what computes texture
   gradients.
3. An ARM data-processing immediate is an 8-bit value rotated right by twice the `rot`
   field. capstone resolves that for canonical encodings but reports the two halves
   separately for non-canonical ones -- `sub r0, r0, #4, #24` really means
   `sub r0, r0, #0x400` -- and taking `op.imm` gave 4. Exactly **three** instructions in
   the whole game are encoded that way, all of them PIC adjustments that find a data
   table; the result was that the trig tables were read from the wrong address, so the
   bike model's polygons came out degenerate and it rendered as an outline or not at all.
   The recompiler now decodes every data-processing immediate straight from the
   instruction word.

## Finding bug 3

Bugs 1 and 2 came out of a whole-run trace diff, but that drifts once the two engines run
at different speeds. Bug 3 needed something exact, so the tooling grew three pieces:

- **A headless display driver** (`vendor/PumpkinOS/src/libwtest`) that keeps the
  framebuffer in memory, writes PPM snapshots and takes input from a text script -- so
  runs are reproducible and need no window.
- **Per-blob and per-range engine selection** (`BOD_ARM_BLOBS`, `BOD_ARM_IBLOB/ILO/IHI`)
  plus `tools/bikecheck.py`, which scores a frame for how much of the (red) bike is
  visible. That makes bisection automatic. One trap: the interpreter hands back control
  only at batch boundaries, so a large batch spills execution well past the selected
  range and the bisection converges on the wrong place. `BOD_ARM_BATCH=1` gives exact
  range semantics.
- **A lockstep verifier** (`BOD_ARM_LOCKSTEP=<blob>`). The generator emits a second
  variant of a blob -- same translation, but with an empty in-range window so every
  branch and fall-through becomes "set r15, save, return", i.e. a single-step core. The
  verifier then runs one instruction under the interpreter and the same instruction under
  the stepper from the same input state and compares registers. That reports the exact
  program counter and the exact register that disagrees, with no drift at all.

The lockstep run named `armc2 + 0x8A8` on the first try. Afterwards it reports zero
mismatches, and the game renders identically on both engines.
