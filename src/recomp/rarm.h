/* Runtime support for the statically recompiled ARM32 code (the `armc`
 * resources) of Bike or Die 2.  The ARM side of a Palm OS 5 application runs
 * little-endian over the same address space the big-endian 68k code uses. */
#ifndef RARM_H
#define RARM_H

#include <stdint.h>

typedef struct {
  uint32_t r[16];              /* r15 is only meaningful across dispatch */
  uint32_t nf, zf, cf, vf;
  int      halt;               /* set when the armlet returned or the app ended */
  uint32_t result;             /* r0 at return */
} rarm_state;

extern uint8_t *rarm_ram;
extern uint32_t rarm_ramsize;
extern uint32_t rarm_segbase[8];   /* runtime address of each armc resource */
extern uint32_t rarm_segsize[8];

/* RECOMP_GUARD turns wild memory accesses into a diagnostic naming the ARM
 * instruction that made them, instead of a segfault. */
extern uint32_t rarm_cur_pc;
void rarm_badaddr(uint32_t addr, uint32_t size, int write);
/* Instruction trace, used to diff against the interpreter. */
extern int rarm_tracing;
void rarm_trace(rarm_state *S);
#ifdef RECOMP_GUARD
#define ACHK(a, n, w) do { if ((uint64_t)(a) + (n) > rarm_ramsize) { rarm_badaddr((a), (n), (w)); return; } } while (0)
#define ACHKR(a, n) do { if ((uint64_t)(a) + (n) > rarm_ramsize) { rarm_badaddr((a), (n), 0); return 0; } } while (0)
#else
#define ACHK(a, n, w)  ((void)0)
#define ACHKR(a, n)    ((void)0)
#endif

/* ---- little-endian memory access ---- */
static inline uint32_t ard8 (uint32_t a) { ACHKR(a, 1); return rarm_ram[a]; }
static inline uint32_t ard16(uint32_t a) { ACHKR(a, 2); return rarm_ram[a] | ((uint32_t)rarm_ram[a+1] << 8); }
static inline uint32_t ard32(uint32_t a) {
  ACHKR(a, 4);
  return rarm_ram[a] | ((uint32_t)rarm_ram[a+1] << 8) |
         ((uint32_t)rarm_ram[a+2] << 16) | ((uint32_t)rarm_ram[a+3] << 24);
}
static inline void awr8 (uint32_t a, uint32_t v) { ACHK(a, 1, 1); rarm_ram[a] = (uint8_t)v; }
static inline void awr16(uint32_t a, uint32_t v) { ACHK(a, 2, 1); rarm_ram[a]=(uint8_t)v; rarm_ram[a+1]=(uint8_t)(v>>8); }
static inline void awr32(uint32_t a, uint32_t v) {
  ACHK(a, 4, 1);
  rarm_ram[a]=(uint8_t)v; rarm_ram[a+1]=(uint8_t)(v>>8);
  rarm_ram[a+2]=(uint8_t)(v>>16); rarm_ram[a+3]=(uint8_t)(v>>24);
}

#define ASE8(v)  ((uint32_t)(int32_t)(int8_t)(v))
#define ASE16(v) ((uint32_t)(int32_t)(int16_t)(v))
#define AROR(v,n) ((n) ? (((v) >> (n)) | ((v) << (32 - (n)))) : (v))

/* ---- flags ---- */
#define ANZ(r)   do { nf = ((r) >> 31) & 1u; zf = ((r) == 0); } while (0)
/* res = a + b + ci, with carry/overflow */
#define AADD(a,b,ci,res) do { \
    uint64_t _w = (uint64_t)(uint32_t)(a) + (uint64_t)(uint32_t)(b) + (uint64_t)(uint32_t)(ci); \
    uint32_t _r = (uint32_t)_w; \
    cf = (uint32_t)(_w >> 32); \
    vf = ((~((uint32_t)(a) ^ (uint32_t)(b))) & ((uint32_t)(a) ^ _r)) >> 31; \
    (res) = _r; } while (0)

/* ---- barrel shifter; type: 0=LSL 1=LSR 2=ASR 3=ROR 4=RRX ---- */
static inline uint32_t arm_sh(uint32_t v, int type, uint32_t amt, uint32_t cin, uint32_t *cout) {
  switch (type) {
    case 0:
      if (amt == 0)  { *cout = cin; return v; }
      if (amt < 32)  { *cout = (v >> (32 - amt)) & 1u; return v << amt; }
      if (amt == 32) { *cout = v & 1u; return 0; }
      *cout = 0; return 0;
    case 1:
      if (amt == 0 || amt == 32) { *cout = v >> 31; return 0; }
      if (amt < 32)  { *cout = (v >> (amt - 1)) & 1u; return v >> amt; }
      *cout = 0; return 0;
    case 2:
      if (amt == 0 || amt >= 32) { *cout = v >> 31; return (uint32_t)((int32_t)v >> 31); }
      *cout = (uint32_t)(((int32_t)v >> (amt - 1)) & 1); return (uint32_t)((int32_t)v >> amt);
    case 3: {
      uint32_t a = amt & 31u;
      if (amt == 0) { *cout = cin; return v; }
      if (a == 0)   { *cout = v >> 31; return v; }
      *cout = (v >> (a - 1)) & 1u; return AROR(v, a);
    }
    default: { uint32_t r = (v >> 1) | (cin << 31); *cout = v & 1u; return r; }
  }
}
/* Register-specified shift: an amount of 0 leaves the value and carry alone. */
static inline uint32_t arm_sh_reg(uint32_t v, int type, uint32_t amt, uint32_t cin, uint32_t *cout) {
  amt &= 0xFFu;
  if (amt == 0) { *cout = cin; return v; }
  return arm_sh(v, type, amt, cin, cout);
}

/* ---- condition codes ---- */
#define ACC_AL 1
#define ACC_EQ (zf)
#define ACC_NE (!zf)
#define ACC_HS (cf)
#define ACC_LO (!cf)
#define ACC_MI (nf)
#define ACC_PL (!nf)
#define ACC_VS (vf)
#define ACC_VC (!vf)
#define ACC_HI (cf && !zf)
#define ACC_LS (!cf || zf)
#define ACC_GE (nf == vf)
#define ACC_LT (nf != vf)
#define ACC_GT (!zf && (nf == vf))
#define ACC_LE (zf || (nf != vf))

/* Host callbacks provided by the integration layer. */
/* Control left the recompiled blobs: a return stub, the call-68K trampoline or
 * a PACE syscall. Returns non-zero when execution must stop. */
int  rarm_escape(rarm_state *S, uint32_t target);
void rarm_panic(rarm_state *S, uint32_t pc, const char *why);
int  rarm_resolve(uint32_t pc);
/* Per-blob engine selection, used to bisect translation bugs: a blob that is
 * not selected runs under the interpreter instead. */
int  rarm_blob_recompiled(int n);
int  rarm_pc_recompiled(int n, uint32_t off);
/* Lockstep verification against the interpreter (debug builds). */
int  rarm_lockstep_blob(void);
int  rarm_lockstep(rarm_state *S);
/* Single-step cores are large and only used by the lockstep checker, so they
 * are generated on demand (`recomparm.py --stepper <n>`); this resolves the
 * ones that were, and returns NULL for a blob that has none. */
typedef void (*rarm_step_fn)(rarm_state *S);
rarm_step_fn rarm_stepper(int n);
int  rarm_interp(rarm_state *S);

void rarm_run(rarm_state *S);

#endif
