/* Runtime support for the statically recompiled 68000 code, GBA edition.
 * Same contract as src/recomp/r68k.h, but a 68k address is a real address:
 * the generated code reads and writes memory directly, big-endian. */
#ifndef R68K_H
#define R68K_H
#include <stdint.h>

typedef struct {
  uint32_t d[8], a[8];
  uint32_t pc;
  uint32_t xf, nf, zf, vf, cf;
  int      halt;
  uint32_t ret_sp;
} r68k_state;

extern uint32_t r68k_segbase[4];
extern uint32_t r68k_segsize[4];

static inline uint32_t rd8 (uint32_t a) { return *(const uint8_t *)a; }
static inline uint32_t rd16(uint32_t a) { const uint8_t *p = (const uint8_t *)a; return ((uint32_t)p[0] << 8) | p[1]; }
static inline uint32_t rd32(uint32_t a) {
  const uint8_t *p = (const uint8_t *)a;
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wr8 (uint32_t a, uint32_t v) { *(uint8_t *)a = (uint8_t)v; }
static inline void wr16(uint32_t a, uint32_t v) { uint8_t *p = (uint8_t *)a; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr32(uint32_t a, uint32_t v) {
  uint8_t *p = (uint8_t *)a;
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

#define SE8(v)  ((uint32_t)(int32_t)(int8_t)(v))
#define SE16(v) ((uint32_t)(int32_t)(int16_t)(v))

#define NZ8(r)   do { nf = ((r)>>7)&1;  zf = (((r)&0xFFu)==0); } while (0)
#define NZ16(r)  do { nf = ((r)>>15)&1; zf = (((r)&0xFFFFu)==0); } while (0)
#define NZ32(r)  do { nf = ((r)>>31)&1; zf = (((r)&0xFFFFFFFFu)==0); } while (0)
#define LOGIC8(r)  do { NZ8(r);  vf=0; cf=0; } while (0)
#define LOGIC16(r) do { NZ16(r); vf=0; cf=0; } while (0)
#define LOGIC32(r) do { NZ32(r); vf=0; cf=0; } while (0)
#define ADDF(BITS,d,s,r) do { \
  uint32_t _sm=((s)>>(BITS-1))&1, _dm=((d)>>(BITS-1))&1, _rm=((r)>>(BITS-1))&1; \
  cf = (_sm&_dm) | ((~_rm)&(_sm|_dm)); vf = (_sm==_dm) && (_rm!=_sm); xf = cf; } while (0)
#define SUBF(BITS,d,s,r) do { \
  uint32_t _sm=((s)>>(BITS-1))&1, _dm=((d)>>(BITS-1))&1, _rm=((r)>>(BITS-1))&1; \
  cf = (_sm&(~_dm)) | (_rm&((~_dm)|_sm)); vf = (_sm!=_dm) && (_rm==_sm); } while (0)

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

uint32_t r68k_trap(r68k_state *S, uint16_t trap);
void     r68k_panic(r68k_state *S, uint32_t pc, const char *why);
int      r68k_resolve(uint32_t pc);
int      r68k_interp(r68k_state *S);
void     r68k_run(r68k_state *S);

/* Call a 68k function from C: args is a big-endian block pushed on the 68k
 * stack, the result is D0 (or A0 with wantA0). */
uint32_t r68k_call(uint32_t addr, const void *args, uint32_t argsSize, int wantA0);
#endif
