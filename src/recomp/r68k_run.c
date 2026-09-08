/* Dispatch loop for the statically recompiled 68k code.
 *
 * Code resources 2 and 3 are loaded by the application itself at run time, so
 * their base addresses are discovered lazily through r68k_resolve(). */
#include "r68k.h"
#include "r68k_gen.h"

uint8_t *r68k_ram;
uint32_t r68k_segbase[4];
uint32_t r68k_segsize[4];

static int seg_of(uint32_t pc) {
  for (int n = 1; n < 4; n++)
    if (r68k_segsize[n] && pc >= r68k_segbase[n] && pc < r68k_segbase[n] + r68k_segsize[n])
      return n;
  return -1;
}

void r68k_run(r68k_state *S) {
  while (!S->halt) {
    int n = seg_of(S->pc);
    if (n < 0) {
      if (r68k_resolve(S->pc) == 0) { n = seg_of(S->pc); }
      if (n < 0) {
        /* Not our code: an OS thunk that PumpkinOS synthesised. Interpret it
         * until control comes back to a recompiled segment. */
        if (r68k_interp(S) != 0) return;
        continue;
      }
    }
    uint32_t off = S->pc - r68k_segbase[n];
    int k = (int)(off / R68K_CHUNK_BYTES);
    if (k >= r68k_nchunks[n]) { r68k_panic(S, S->pc, "chunk index out of range"); return; }
    r68k_chunks[n][k](S);
  }
}
