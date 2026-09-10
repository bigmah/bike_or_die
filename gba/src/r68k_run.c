/* Dispatch loop and host glue for the statically recompiled 68k code.
 * Code resources 2 and 3 are loaded by the application itself, so their
 * addresses are discovered lazily. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "debug.h"
#include "emupalmos.h"
#include "r68k.h"
#include "r68k_gen.h"
#include "gba.h"
#include "heap.h"

uint32_t r68k_segbase[4];
uint32_t r68k_segsize[4];
r68k_state *r68k_cur;

static int seg_of(uint32_t pc) {
  int n;
  for (n = 1; n < 4; n++)
    if (r68k_segsize[n] && pc >= r68k_segbase[n] && pc < r68k_segbase[n] + r68k_segsize[n])
      return n;
  return -1;
}

int r68k_resolve(uint32_t pc) {
  MemHandle h;
  uint8_t *p;
  int n;
  for (n = 2; n < 4; n++) {
    if (r68k_segsize[n]) continue;
    if ((h = DmGet1Resource('code', n)) == NULL) continue;
    if ((p = MemHandleLock(h)) != NULL) {
      r68k_segbase[n] = (uint32_t)p;
      r68k_segsize[n] = MemHandleSize(h);
      debug(DEBUG_INFO, "R68K", "code %d at 0x%08X size 0x%X", n, r68k_segbase[n], r68k_segsize[n]);
    }
  }
  return seg_of(pc) < 0 ? -1 : 0;
}

int r68k_interp(r68k_state *S) {
  r68k_panic(S, S->pc, "pc outside the recompiled code");
  return 1;
}

void r68k_panic(r68k_state *S, uint32_t pc, const char *why) {
  debug(DEBUG_ERROR, "R68K", "panic: %s at 0x%08X (a7=0x%08X)", why, pc, S->a[7]);
  emupalmos_panic("68k panic", EMUPALMOS_INVALID_INSTRUCTION);
  S->halt = 1;
}

uint32_t r68k_trap(r68k_state *S, uint16_t trap) {
  r68k_state *saved = r68k_cur;
  uint32_t r;
  const char *ctx = heap_ctx;
  uint32_t t = heap_trap;
  r68k_cur = S;
  heap_ctx = "68k";
  heap_trap = trap;
  r = palmos_systrap(trap);
  heap_trap = t;
  heap_ctx = ctx;
  r68k_cur = saved;
  if (emupalmos_finished()) S->halt = 1;
  return r;
}

void r68k_run(r68k_state *S) {
  while (!S->halt) {
    int n = seg_of(S->pc);
    if (n < 0) {
      if (r68k_resolve(S->pc) == 0) n = seg_of(S->pc);
      if (n < 0) { if (r68k_interp(S)) return; continue; }
    }
    {
      uint32_t off = S->pc - r68k_segbase[n];
      int k = (int)(off / R68K_CHUNK_BYTES);
      if (k >= r68k_nchunks[n]) { r68k_panic(S, S->pc, "chunk index out of range"); return; }
      r68k_chunks[n][k](S);
    }
  }
}

/* Run a 68k function to completion on the current 68k stack: the arguments
 * are pushed below whatever the suspended 68k code has there, a null return
 * address marks the frame, and rts past it stops the loop. */
uint32_t r68k_call(uint32_t addr, const void *args, uint32_t argsSize, int wantA0) {
  r68k_state S, *outer = r68k_cur;
  uint32_t sp;
  if (!outer) { debug(DEBUG_ERROR, "R68K", "r68k_call(0x%08X) with no 68k context", addr); return 0; }
  S = *outer;
  sp = outer->a[7] - 64;              /* leave the trap's own frame alone */
  sp -= argsSize;
  if (argsSize) sys_memcpy((void *)sp, args, argsSize);
  sp -= 4;
  wr32(sp, 0);
  S.a[7] = sp;
  S.ret_sp = sp;
  S.pc = addr;
  S.halt = 0;
  r68k_cur = &S;
  r68k_run(&S);
  r68k_cur = outer;
  if (emupalmos_finished()) outer->halt = 1;
  return wantA0 ? S.a[0] : S.d[0];
}
