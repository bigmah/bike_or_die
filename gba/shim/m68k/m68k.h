/* Musashi's register and memory API, over the statically recompiled 68k
 * state. The PumpkinOS trap marshalling code reads its arguments through
 * these, so they map straight onto the recompiled core's register file and
 * onto raw (big-endian) memory. */
#ifndef M68K_SHIM_H
#define M68K_SHIM_H
#include <stdint.h>
#include "r68k.h"

enum {
  M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
  M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
  M68K_REG_PC, M68K_REG_SR, M68K_REG_SP, M68K_REG_USP, M68K_REG_ISP
};

typedef struct { int finish; } m68k_state_t;

extern r68k_state *r68k_cur;   /* the 68k thread currently inside a trap */

static inline uint32_t m68k_get_reg(void *ctx, int reg) {
  r68k_state *S = r68k_cur;
  (void)ctx;
  if (reg < 8) return S->d[reg];
  if (reg < 16) return S->a[reg - 8];
  switch (reg) {
    case M68K_REG_PC: return S->pc;
    case M68K_REG_SP: case M68K_REG_USP: case M68K_REG_ISP: return S->a[7];
    case M68K_REG_SR: return (S->xf << 4) | (S->nf << 3) | (S->zf << 2) | (S->vf << 1) | S->cf;
  }
  return 0;
}
static inline void m68k_set_reg(int reg, uint32_t v) {
  r68k_state *S = r68k_cur;
  if (reg < 8) { S->d[reg] = v; return; }
  if (reg < 16) { S->a[reg - 8] = v; return; }
  switch (reg) {
    case M68K_REG_PC: S->pc = v; break;
    case M68K_REG_SP: case M68K_REG_USP: case M68K_REG_ISP: S->a[7] = v; break;
    case M68K_REG_SR: S->cf = v & 1; S->vf = (v >> 1) & 1; S->zf = (v >> 2) & 1; S->nf = (v >> 3) & 1; S->xf = (v >> 4) & 1; break;
  }
}
static inline uint32_t m68k_read_memory_8 (uint32_t a) { return rd8(a); }
static inline uint32_t m68k_read_memory_16(uint32_t a) { return rd16(a); }
static inline uint32_t m68k_read_memory_32(uint32_t a) { return rd32(a); }
static inline void m68k_write_memory_8 (uint32_t a, uint32_t v) { wr8(a, v); }
static inline void m68k_write_memory_16(uint32_t a, uint32_t v) { wr16(a, v); }
static inline void m68k_write_memory_32(uint32_t a, uint32_t v) { wr32(a, v); }
static inline void m68k_pulse_halt(void) { if (r68k_cur) r68k_cur->halt = 1; }
#endif
