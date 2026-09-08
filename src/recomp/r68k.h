/* Runtime support for the statically recompiled Motorola 68000 code of
 * Bike or Die 2.  The generated C keeps the register file in locals and
 * only syncs through this struct at chunk boundaries and around traps. */
#ifndef R68K_H
#define R68K_H

#include <stdint.h>

typedef struct {
  uint32_t d[8], a[8];
  uint32_t pc;
  uint32_t xf, nf, zf, vf, cf;   /* CCR, one flag per word */
  int      halt;                 /* set when the entry frame returns */
  uint32_t ret_sp;               /* stack pointer that marks "returned" */
} r68k_state;

/* Base of the emulated Palm address space (PumpkinOS heap). */
extern uint8_t *r68k_ram;
/* Runtime load address of each 'code' resource, index 1..3. */
extern uint32_t r68k_segbase[4];
extern uint32_t r68k_segsize[4];

/* ---- big-endian memory access ---- */
static inline uint32_t rd8 (uint32_t a) { return r68k_ram[a]; }
static inline uint32_t rd16(uint32_t a) { return ((uint32_t)r68k_ram[a]<<8) | r68k_ram[a+1]; }
static inline uint32_t rd32(uint32_t a) {
  return ((uint32_t)r68k_ram[a]<<24) | ((uint32_t)r68k_ram[a+1]<<16) |
         ((uint32_t)r68k_ram[a+2]<<8) | r68k_ram[a+3];
}
static inline void wr8 (uint32_t a, uint32_t v) { r68k_ram[a] = (uint8_t)v; }
static inline void wr16(uint32_t a, uint32_t v) { r68k_ram[a]=(uint8_t)(v>>8); r68k_ram[a+1]=(uint8_t)v; }
static inline void wr32(uint32_t a, uint32_t v) {
  r68k_ram[a]=(uint8_t)(v>>24); r68k_ram[a+1]=(uint8_t)(v>>16);
  r68k_ram[a+2]=(uint8_t)(v>>8); r68k_ram[a+3]=(uint8_t)v;
}

/* ---- sign extension ---- */
#define SE8(v)  ((uint32_t)(int32_t)(int8_t)(v))
#define SE16(v) ((uint32_t)(int32_t)(int16_t)(v))

/* ---- flag helpers; operate on the generated code's local flag words ---- */
#define NZ8(r)   do { nf = ((r)>>7)&1;  zf = (((r)&0xFFu)==0); } while (0)
#define NZ16(r)  do { nf = ((r)>>15)&1; zf = (((r)&0xFFFFu)==0); } while (0)
#define NZ32(r)  do { nf = ((r)>>31)&1; zf = (((r)&0xFFFFFFFFu)==0); } while (0)
#define LOGIC8(r)  do { NZ8(r);  vf=0; cf=0; } while (0)
#define LOGIC16(r) do { NZ16(r); vf=0; cf=0; } while (0)
#define LOGIC32(r) do { NZ32(r); vf=0; cf=0; } while (0)

/* add: r = d + s (+x) */
#define ADDF(BITS,d,s,r) do { \
  uint32_t _sm=((s)>>(BITS-1))&1, _dm=((d)>>(BITS-1))&1, _rm=((r)>>(BITS-1))&1; \
  cf = (_sm&_dm) | ((~_rm)&(_sm|_dm)); vf = (_sm==_dm) && (_rm!=_sm); xf = cf; } while (0)
/* sub/cmp: r = d - s */
#define SUBF(BITS,d,s,r) do { \
  uint32_t _sm=((s)>>(BITS-1))&1, _dm=((d)>>(BITS-1))&1, _rm=((r)>>(BITS-1))&1; \
  cf = (_sm&(~_dm)) | (_rm&((~_dm)|_sm)); vf = (_sm!=_dm) && (_rm==_sm); } while (0)

/* ---- condition codes ---- */
#define CC_T   (1)
#define CC_F   (0)
#define CC_HI  (!cf && !zf)
#define CC_LS  (cf || zf)
#define CC_CC  (!cf)
#define CC_CS  (cf)
#define CC_NE  (!zf)
#define CC_EQ  (zf)
#define CC_VC  (!vf)
#define CC_VS  (vf)
#define CC_PL  (!nf)
#define CC_MI  (nf)
#define CC_GE  (nf == vf)
#define CC_LT  (nf != vf)
#define CC_GT  (!zf && (nf == vf))
#define CC_LE  (zf || (nf != vf))

/* Host callbacks provided by the integration layer. */
uint32_t r68k_trap(r68k_state *S, uint16_t trap);   /* TRAP #15 -> PalmOS */
void     r68k_panic(r68k_state *S, uint32_t pc, const char *why);

/* Locate the code resource containing pc and fill in r68k_segbase/size.
 * Returns 0 on success. */
int  r68k_resolve(uint32_t pc);
/* Interpret non-recompiled 68k (PumpkinOS's own thunks) until control returns
 * to a recompiled segment. Returns non-zero if execution should stop. */
int  r68k_interp(r68k_state *S);

/* Dispatch: run until the app finishes. */
void r68k_run(r68k_state *S);

#endif
