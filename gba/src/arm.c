/* Running the game's ARM code natively. Palm OS 5 hands an ARMlet three
 * things: the emulator state, its 68k argument block, and a function to call
 * back into 68k land; PACE syscalls go through a table of tables in r9.
 * Here the tables point at real code, the emulator state is the recompiled
 * 68k register file, and the callback runs the trap dispatcher directly. */
#include <PalmOS.h>
#include <PceNativeCall.h>
#include "sys.h"
#include "pumpkin.h"
#include "debug.h"
#include "emupalmos.h"
#include "libc.h"
#include "gba.h"
#include "r68k.h"
#include "heap.h"

extern uint32_t pce_call(uint32_t code, uint32_t userData, uint32_t emulState, uint32_t call68k, uint32_t r9);
extern const uint32_t syscall_tables[3072];
extern void call68k_arm(void);
uint32_t emupalmos_arm_syscall(uint32_t group, uint32_t function, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp);

/* The master table: four group pointers, then the two words the sound
 * callback's prologue chases (ldr r9,[r9]; ldr r9,[r9,#2052]). */
static const uint32_t systable[520] = {
  [1] = (uint32_t)&syscall_tables[0],
  [2] = (uint32_t)&syscall_tables[1024],
  [3] = (uint32_t)&syscall_tables[2048],
  [4] = (uint32_t)systable,
  [517] = (uint32_t)systable + 16,
};

static uint32_t emulstate[64];
static int pce_depth;

extern volatile uint32_t vblank_count;
extern int64_t sys_get_clock(void);
void bod_engine_install(void);
uint32_t bod_cur_sys;
int64_t bod_sys_usec;

static uint32_t pce_calls, pce_last_vblank;
static int64_t pce_usec;
uint32_t bod_evt_calls, bod_mm_rows[4];

uint32_t arm_native_call_pce(uint32_t code, uint32_t userData) {
  r68k_state *S = r68k_cur;
  uint32_t r, i;
  int64_t t0 = sys_get_clock();
  bod_engine_install();
  pce_calls++;
  if (vblank_count - pce_last_vblank >= 300) {
    extern uint32_t bod_mm_bytes[4], bod_mm_rows[4];
    { uint32_t hf, hl, hu; heap_stats(&hf, &hl, &hu);
    debug(DEBUG_INFO, "ARM", "%u engine calls (%u ms) and %u event polls in %u frames; heap free %u largest %u chunks %u", pce_calls, (uint32_t)(pce_usec / 1000), bod_evt_calls, vblank_count - pce_last_vblank, hf, hl, hu); }
    {
      extern struct bod_trap_time { uint16_t trap; uint16_t n; uint32_t usec; } bod_trap_time[48];
      int i, k;
      for (k = 0; k < 5; k++) {
        int best = -1;
        for (i = 0; i < 48; i++) if (bod_trap_time[i].trap && (best < 0 || bod_trap_time[i].usec > bod_trap_time[best].usec)) best = i;
        if (best < 0 || bod_trap_time[best].usec < 20000) break;
        debug(DEBUG_INFO, "ARM", "  trap 0x%04X: %u calls, %u ms", bod_trap_time[best].trap, bod_trap_time[best].n, bod_trap_time[best].usec / 1000);
        bod_trap_time[best].usec = 0;
      }
      for (i = 0; i < 48; i++) { bod_trap_time[i].trap = 0; bod_trap_time[i].n = 0; bod_trap_time[i].usec = 0; }
    }
    pce_last_vblank = vblank_count; pce_calls = 0; pce_usec = 0; bod_evt_calls = 0; bod_sys_usec = 0;
    bod_mm_bytes[0] = bod_mm_bytes[1] = bod_mm_bytes[2] = bod_mm_bytes[3] = 0;
  }
  emulstate[0] = 0;
  if (S) {
    for (i = 0; i < 8; i++) { emulstate[1 + i] = S->d[i]; emulstate[9 + i] = S->a[i]; }
    emulstate[17] = S->pc;
  }
  pce_depth++;
  r = pce_call(code, userData, (uint32_t)emulstate, (uint32_t)call68k_arm, (uint32_t)systable + 16);
  pce_depth--;
  pce_usec += sys_get_clock() - t0;
  return r;
}

uint32_t arm_native_call_sub(uint32_t code, uint32_t data, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {
  (void)code; (void)data; (void)p0; (void)p1; (void)p2; (void)p3;
  return 0;
}

/* Call68KFuncType: a trap, or a 68k function, with a big-endian argument
 * block the ARM code built. */
uint32_t call68K_func(uint32_t emulStateP, uint32_t trapOrFunction, uint32_t argsOnStackP, uint32_t argsSizeAndwantA0) {
  uint32_t argsSize = argsSizeAndwantA0 & ~kPceNativeWantA0;
  int wantA0 = (argsSizeAndwantA0 & kPceNativeWantA0) != 0;
  r68k_state *S = r68k_cur;
  uint32_t r, sp, saved;

  if (trapOrFunction < kPceNativeTrapNoMask) {
    if (!S) return 0;
    saved = S->a[7];
    sp = saved - argsSize;
    if (argsSize) memcpy((void *)sp, (const void *)argsOnStackP, argsSize);
    S->a[7] = sp;
    if (emulStateP) S->d[2] = ((uint32_t *)emulStateP)[3];   /* the dispatch selector */
    palmos_systrap(0xA000 | (trapOrFunction & 0xFFF));
    r = wantA0 ? S->a[0] : S->d[0];
    S->a[7] = saved;
    return r;
  }
  return r68k_call(trapOrFunction, (const void *)argsOnStackP, argsSize, wantA0);
}

extern uint32_t *arm_syscall_regs;   /* the caller's r4-r11, lr */
uint32_t bod_texhook_alloc(uint32_t size, const uint32_t *regs);
uint32_t bod_edge_hook(uint32_t ctx);

uint32_t arm_syscall_dispatch(uint32_t index, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
  uint32_t group = index / 1024 + 1, function = (index % 1024) * 4, r;
  extern uint32_t bod_cur_sys;
int64_t bod_sys_usec;
  { extern uint32_t arm_syscall_lr; volatile uint32_t *dbg = (volatile uint32_t *)0x030053F0; dbg[0] = group; dbg[1] = function; dbg[2] = arm_syscall_lr; dbg[3] = r0; }
  bod_cur_sys = function;
  const char *ctx = heap_ctx;
  if (group == 1 && function >= 0xF00) {
    /* the patched engine's own entry points (tools/gba/patches) */
    switch (function) {
      case 0xFF8: return bod_edge_hook(r0);
      default: return 0;
    }
  }
  if (group == 2) {
    /* the level parser reads its data four bytes at a time through MemMove */
    if (function == 0x558) { extern int bod_mm_arm; bod_mm_arm = 1; MemMove((void *)r0, (const void *)r1, (Int32)r2); bod_mm_arm = 0; return 0; }
    if (function == 0x5B0) { MemSet((void *)r0, (Int32)r1, (UInt8)r2); return 0; }
    if (function == 0x584 && r0 >= 1024) {
      /* MemPtrNew: texture buffers come from ROM */
      if ((r = bod_texhook_alloc(r0, arm_syscall_regs)) != 0) return r;
    } else if (function == 0x434) {
      return 0x0F02;   /* FtrPtrNew: no feature memory to move chunks into */
    }
  }
  heap_ctx = "arm";
  { int64_t t0 = sys_get_clock(); r = emupalmos_arm_syscall(group, function, r0, r1, r2, r3, sp); bod_sys_usec += sys_get_clock() - t0; }
  heap_ctx = ctx;
  bod_cur_sys = 0;
  return r;
}
