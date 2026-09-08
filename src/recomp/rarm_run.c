/* Dispatch loop for the statically recompiled ARM32 code. */
#include "rarm.h"
#include "rarm_gen.h"

uint8_t *rarm_ram;
uint32_t rarm_ramsize;
uint32_t rarm_cur_pc;
int rarm_tracing;

void *rarm_dbg_tracing_addr(void) { return &rarm_tracing; }
uint32_t rarm_segbase[8];
uint32_t rarm_segsize[8];

static int seg_of(uint32_t pc) {
  for (int n = 1; n < 8; n++)
    if (rarm_segsize[n] && pc >= rarm_segbase[n] && pc < rarm_segbase[n] + rarm_segsize[n])
      return n;
  return -1;
}

void rarm_run(rarm_state *S) {
  while (!S->halt) {
    uint32_t pc = S->r[15];
    int n = seg_of(pc);
    if (n < 0) {
      if (rarm_resolve(pc) == 0) n = seg_of(pc);
      if (n < 0) {
        /* Left the recompiled blobs: a return stub, the call-68K trampoline,
         * or a PACE syscall. */
        if (rarm_escape(S, pc)) return;
        continue;
      }
    }
    if (n == rarm_lockstep_blob()) {
      if (rarm_lockstep(S)) return;
      continue;
    }
    if (!rarm_pc_recompiled(n, pc - rarm_segbase[n])) {
      if (rarm_interp(S)) return;
      continue;
    }
    {
      uint32_t off = pc - rarm_segbase[n];
      int k = (int)(off / RARM_CHUNK_BYTES);
      if (k >= rarm_nchunks[n]) { rarm_panic(S, pc, "chunk index out of range"); return; }
      rarm_chunks[n][k](S);
    }
  }
}
