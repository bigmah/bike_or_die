/* Textures from ROM.
 *
 * The engine tints each texture at level load: it doubles the tex0 source,
 * maps grey through the level's colour gradient (a lookup table, or error
 * diffusion when the gradient is RGB), and builds mipmaps. That needs half
 * a megabyte per level. On the GBA the grey images live in ROM (see
 * tools/gba/pretex.py) and the patched sampler tints texels through a pair
 * of lookup tables per texture slot (tools/gba/patches/armc2.s), so what
 * this file does is answer the loader's buffer requests with ROM addresses
 * and build the tables. The requests are told apart by their call site in
 * the `armc` 3 resource, found by walking the APCS frame chain from the
 * syscall. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "mutex.h"
#include "storage.h"
#include "debug.h"
#include "heap.h"
#include "libc.h"
#include "gba.h"
#include "bod.h"

typedef struct {
  uint32_t tex_addr; uint16_t w, h;
  uint32_t gry1, grm1, gry2, grm2, gry3, grm3;
  uint8_t avg1, avg2, avg3, pad;
  uint32_t reserved;
} txix_t;

/* loader call sites (offsets into armc 3) that ask for texture memory */
#define SITE_DOUBLE   0x2d70   /* sub_2d38: 4*w*h, plain doubling */
#define SITE_SMOOTH   0x3504   /* sub_2d38 called from sub_34a0: smoothed */
#define SITE_SINGLE   0x3f54   /* sub_3c90: w*h, texel copy */
#define SITE_MIPS     0x3aa8   /* sub_3a34: the mipmap pyramid */
#define SITE_TABLE    0x4f04   /* the level's texture table, 144 bytes each */

#define LUT_BASE   0x03000400u
#define LUT_SLOTS  12
#define LUT_STRIDE 768
#define DITHER     10          /* +- this much RGB per checkerboard phase */

static const txix_t *txix;
static int ntxix, txix_tried;
static uint32_t armc3;
static uint32_t tex_table;
static struct { uint32_t base, mips; uint8_t avg; } served[LUT_SLOTS + 4];   /* a ring of what was handed out */
static int nserved;
static uint8_t *rgb444;    /* 4096-entry nearest palette index cache */

static void txix_init(void) {
  LocalID id;
  DmOpenRef db;
  UInt16 index;
  MemHandle h;
  if (txix_tried) return;
  txix_tried = 1;
  if ((id = DmFindDatabase(0, "Precomputed")) == 0 || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == NULL) { debug(DEBUG_ERROR, "TEX", "no Precomputed database"); return; }
  if ((index = DmFindResource(db, 'txix', 1, NULL)) == 0xFFFF || (h = DmGetResourceIndex(db, index)) == NULL) { debug(DEBUG_ERROR, "TEX", "no texture index"); return; }
  {
    const uint32_t *p = MemHandleLock(h);
    if (p[0] != 0x58495854) { debug(DEBUG_ERROR, "TEX", "bad texture index"); return; }
    ntxix = p[1];
    txix = (const txix_t *)(p + 2);
    debug(DEBUG_INFO, "TEX", "%d precomputed textures", ntxix);
  }
}

static uint32_t armc3_base(void) {
  if (!armc3) {
    MemHandle h = DmGet1Resource('armc', 3);
    if (h) armc3 = (uint32_t)MemHandleLock(h);
  }
  return armc3;
}

/* The precomputed entry for a tex0 struct: [8] texels, [0xe] w, [0x10] h. */
static const txix_t *find_tex(uint32_t t) {
  uint32_t pixels = *(const uint32_t *)(t + 8);
  int w = *(const int16_t *)(t + 0xe), h = *(const int16_t *)(t + 0x10), i;
  txix_init();
  for (i = 0; i < ntxix; i++) {
    const txix_t *e = &txix[i];
    if (e->w != w || e->h != h) continue;
    if (pixels >= e->tex_addr && pixels < e->tex_addr + 64 + (uint32_t)w * h) return e;
    if (pixels < 0x08000000 && memcmp((const void *)pixels, (const void *)e->gry1, 64) == 0) return e;
  }
  debug(DEBUG_ERROR, "TEX", "unknown texture %dx%d texels at 0x%08X", w, h, pixels);
  return NULL;
}

static uint32_t serve(uint32_t base, uint32_t mips, uint8_t avg, const char *what) {
  int i;
  for (i = 0; i < LUT_SLOTS + 4; i++) if (served[i].base == base) break;
  if (i == LUT_SLOTS + 4) { i = nserved; nserved = (nserved + 1) % (LUT_SLOTS + 4); served[i].base = base; served[i].mips = mips; served[i].avg = avg; }
  debug(DEBUG_INFO, "TEX", "%s texture from ROM 0x%08X", what, base);
  return base;
}

static uint8_t nearest(int r, int g, int b) {
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;
  if (!rgb444) {
    /* the table is normally precomputed from the system palette (tools/gba/pretex.py) */
    LocalID id;
    DmOpenRef db;
    UInt16 index;
    MemHandle h;
    if ((id = DmFindDatabase(0, "Precomputed")) != 0 && (db = DmOpenDatabase(0, id, dmModeReadOnly)) != NULL &&
        (index = DmFindResource(db, 'pal4', 1, NULL)) != 0xFFFF && (h = DmGetResourceIndex(db, index)) != NULL)
      rgb444 = MemHandleLock(h);
  }
  if (!rgb444) {
    RGBColorType *pal = WinGetPalette(256);
    int i, k;
    rgb444 = MemPtrNew(4096);
    if (!rgb444 || !pal) { rgb444 = NULL; return 0; }
    for (k = 0; k < 4096; k++) {
      int kr = ((k >> 8) & 15) * 17, kg = ((k >> 4) & 15) * 17, kb = (k & 15) * 17, best = 0;
      uint32_t bd = 0xFFFFFFFF;
      for (i = 0; i < 256; i++) {
        int dr = kr - pal[i].r, dg = kg - pal[i].g, db = kb - pal[i].b;
        uint32_t d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (d < bd) { bd = d; best = i; }
      }
      rgb444[k] = best;
    }
  }
  return rgb444[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)];
}

/* Fill the slot's tables from what the loader left in the engine context:
 * kind != 0 means the texture is palette mapped (the table at ctx+0x649c is
 * exact); kind 0 means an RGB gradient at ctx+0x589c that the Palm would
 * have error-diffused, approximated here with a two-phase dither. */
static void build_lut(int slot, uint32_t ts, uint32_t ctx) {
  uint8_t *A = (uint8_t *)(LUT_BASE + slot * LUT_STRIDE), *B = A + 256;
  uint8_t kind = *(const uint8_t *)ts;
  int g;
  if (kind) {
    memcpy(A, (const void *)(ctx + 0x649c), 256);
    memcpy(B, A, 256);
  } else {
    const uint8_t *grad = (const uint8_t *)(ctx + 0x589c);
    for (g = 0; g < 256; g++) {
      int b = grad[4 * g], gr = grad[4 * g + 1], r = grad[4 * g + 2];
      A[g] = nearest(r + DITHER, gr + DITHER, b + DITHER);
      B[g] = nearest(r - DITHER, gr - DITHER, b - DITHER);
    }
  }
  memcpy(A + 512, A, 256);
  debug(DEBUG_INFO, "TEX", "slot %d kind %d A: %02X %02X %02X %02X %02X %02X %02X %02X .. %02X %02X  B: %02X %02X %02X %02X", slot, kind,
        A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[30], A[31], B[0], B[1], B[2], B[3]);
  if (!kind) { const uint8_t *grad = (const uint8_t *)(ctx + 0x589c); debug(DEBUG_INFO, "TEX", "  gradient BGR %02X%02X%02X %02X%02X%02X %02X%02X%02X .. %02X%02X%02X", grad[0], grad[1], grad[2], grad[4], grad[5], grad[6], grad[8], grad[9], grad[10], grad[124], grad[125], grad[126]); }
}

/* Called for every MemPtrNew the ARM code makes, before the allocation.
 * Returns the address to hand back, or 0 to allocate normally. regs holds
 * the caller's r4-r11 and lr as saved by the syscall stub. */
uint32_t bod_texhook_alloc(uint32_t size, const uint32_t *regs) {
  uint32_t fp0 = regs[7], fp1, lr2, base, site;
  const txix_t *e;
  if ((fp0 >> 24) != 0x02 && (fp0 >> 24) != 0x03) return 0;
  if ((base = armc3_base()) == 0) return 0;
  lr2 = *(const uint32_t *)(fp0 - 4);
  if (lr2 < base || lr2 - base >= 0x10000) return 0;
  site = lr2 - base;
  fp1 = *(const uint32_t *)(fp0 - 12);
  if (size >= 1024) debug(DEBUG_INFO, "TEX", "MemPtrNew(%u) from armc3+0x%04X", size, site);
  switch (site) {
    case SITE_TABLE: {
      uint32_t p = (uint32_t)MemPtrNew(size);
      tex_table = p;
      nserved = 0;
      debug(DEBUG_INFO, "TEX", "texture table 0x%08X, %u entries", p, size / 144);
      return p;
    }
    case SITE_DOUBLE: {
      uint32_t lr3 = *(const uint32_t *)(fp1 - 4);
      int smooth = (lr3 - base) == SITE_SMOOTH;
      if ((e = find_tex(regs[0])) == NULL || size != 4u * e->w * e->h) return 0;
      return smooth ? serve(e->gry3, e->grm3, e->avg3, "smoothed") : serve(e->gry2, e->grm2, e->avg2, "doubled");
    }
    case SITE_SINGLE: {
      uint32_t t = *(const uint32_t *)(fp1 - 0x74);
      if ((e = find_tex(t)) == NULL || size != (uint32_t)e->w * e->h) return 0;
      return serve(e->gry1, e->grm1, e->avg1, "single");
    }
    case SITE_MIPS: {
      /* sub_3a34's frame holds the context; its caller sub_3c90's frame holds the level's
       * texture descriptor, whose index in the descriptor array is the slot the renderer uses */
      uint32_t ts = regs[4], b = *(const uint32_t *)(ts + 0x1c), ctx = *(const uint32_t *)(fp1 - 0x24);
      uint32_t fp2 = *(const uint32_t *)(fp1 - 12), desc = *(const uint32_t *)(fp2 - 0x68), dbase = *(const uint32_t *)(ctx + 0x5828);
      int i, slot;
      for (i = 0; i < LUT_SLOTS + 4; i++) if (served[i].base == b) break;
      if (i == LUT_SLOTS + 4) { debug(DEBUG_ERROR, "TEX", "mipmaps for unknown texture 0x%08X", b); return 0; }
      slot = (desc >= dbase && desc - dbase < 64 * 64) ? (int)((desc - dbase) / 64) : -1;
      if (slot < 0 || slot >= LUT_SLOTS) { debug(DEBUG_ERROR, "TEX", "texture slot %d out of range", slot); return 0; }
      build_lut(slot, ts, ctx);
      if (*(const uint8_t *)ts == 0) *(uint32_t *)(ts + 4 + 0x14) = *(const uint8_t *)(LUT_BASE + slot * LUT_STRIDE + served[i].avg);
      debug(DEBUG_INFO, "TEX", "slot %d: kind %d mipmaps 0x%08X", slot, *(const uint8_t *)ts, served[i].mips);
      return served[i].mips;
    }
    default:
      return 0;
  }
}
