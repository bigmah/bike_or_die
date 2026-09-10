/* Bringing the hardware up: timers for the tick clock, the 8-bit display,
 * the heap, storage, and the PumpkinOS modules. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "mutex.h"
#include "storage.h"
#include "bod.h"
#include "ColorTable.h"
#include "debug.h"
#include "heap.h"
#include "libc.h"
#include "gba.h"

void storage_init(void);
void storage_load(void);
void pumpkin_load_fonts(void);
int UicInitModule(void);

/* ---- time: timer 0 cascaded into timer 1 counts at 16384 Hz ---- */
static void timer_init(void) {
  REG_TM0CNT_H = 0;
  REG_TM1CNT_H = 0;
  REG_TM0CNT_L = 0;
  REG_TM1CNT_L = 0;
  REG_TM1CNT_H = 0x84;          /* count-up (cascade), enabled */
  REG_TM0CNT_H = 0x83;          /* prescaler 1024 -> 16384 Hz, enabled */
}

static inline u32 timer_ticks16k(void) {
  u16 hi1, lo, hi2;
  do { hi1 = REG_TM1CNT_L; lo = REG_TM0CNT_L; hi2 = REG_TM1CNT_L; } while (hi1 != hi2);
  return ((u32)hi1 << 16) | lo;
}

int64_t sys_get_clock(void) {              /* microseconds */
  u32 t = timer_ticks16k();
  return ((int64_t)t * 1000000) / 16384;
}


/* ---- the display: mode 4, one 8-bit page, the Palm system palette ---- */
/* Copy an 8-bit rectangle into the display page: VRAM takes no byte writes,
 * so the ends of each row are read-modify-write halfwords. */
void screen_copy_rect(const uint8_t *bits, int rowBytes, int x, int y, int w, int h) {
  int row;
  for (row = 0; row < h; row++) {
    const uint8_t *s = bits + row * rowBytes;
    int dx = x, n = w;
    vu16 *hw;
    if (dx & 1) {
      hw = (vu16 *)(VRAM_PAGE0 + (y + row) * 240 + dx - 1);
      *hw = (u16)((*hw & 0x00FF) | (s[0] << 8));
      dx++; s++; n--;
    }
    hw = (vu16 *)(VRAM_PAGE0 + (y + row) * 240 + dx);
    if ((uint32_t)s & 1) {
      while (n >= 2) { *hw++ = (u16)(s[0] | (s[1] << 8)); s += 2; n -= 2; }
    } else {
      const u16 *sh = (const u16 *)s;
      while (n >= 2) { *hw++ = *sh++; n -= 2; }
      s = (const uint8_t *)sh;
    }
    if (n) *hw = (u16)((*hw & 0xFF00) | s[0]);
  }
}

void screen_set_palette(const RGBColorType *table, int n) {
  int i;
  for (i = 0; i < n && i < 256; i++) PALRAM[i] = RGB15(table[i].r >> 3, table[i].g >> 3, table[i].b >> 3);
}

static void display_init(void) {
  int i;
  REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
  for (i = 0; i < 256; i++) PALRAM[i] = 0;
  PALRAM[0] = RGB15(31, 31, 31);
  memset((void *)VRAM_PAGE0, 0, 240 * 160);
  REG_DISPSTAT = 0x0008;          /* vblank interrupt: the handler in crt0.s counts frames */
  REG_IE = IRQ_VBLANK;
  REG_IME = 1;
}

/* A Palm bitmap header whose pixels live somewhere else (ROM glyph rows,
 * VRAM): a version 3 header with the indirect flag and the address after it.
 * Bitmap.c reads that address in the bitmap's own byte order; write it the
 * way it reads it. */
BitmapType *bod_indirect_bitmap(Coord width, Coord height, UInt16 rowBytes, UInt8 depth, Boolean transparent, void *bits) {
  uint8_t *b = MemPtrNew(24 + 4);
  if (!b) return NULL;
  b[0] = width >> 8; b[1] = (uint8_t)width;
  b[2] = height >> 8; b[3] = (uint8_t)height;
  b[4] = rowBytes >> 8; b[5] = (uint8_t)rowBytes;
  b[6] = 0x10 | (transparent ? 0x20 : 0); b[7] = 0;   /* indirect, hasTransparency */
  b[8] = depth; b[9] = 3; b[10] = 24; b[11] = 0; b[12] = 0; b[13] = 0;
  b[14] = 0; b[15] = 72;                                /* density */
  b[16] = b[17] = b[18] = b[19] = 0;                    /* transparent value */
  b[20] = b[21] = b[22] = b[23] = 0;                    /* next bitmap */
  b[24] = (uint32_t)bits >> 24; b[25] = (uint32_t)bits >> 16; b[26] = (uint32_t)bits >> 8; b[27] = (uint32_t)bits;
  if (BmpGetBits((BitmapType *)b) != bits) *(uint32_t *)(b + 24) = (uint32_t)bits;
  if (BmpGetBits((BitmapType *)b) != bits) debug(DEBUG_ERROR, "PLATFORM", "indirect bitmap does not resolve");
  return (BitmapType *)b;
}

/* Re-point the display window's bitmap at VRAM: same header and colour
 * table, indirect bits. Saves 38 KB of EWRAM and a copy per update. */
static void display_to_vram(void) {
  WinHandle wh = WinGetDisplayWindow();
  uint8_t *old, *bmp;
  uint32_t hdr = 24 + 2 + 256 * 4;
  if (!wh || !wh->bitmapP) return;
  old = (uint8_t *)wh->bitmapP;
  if ((bmp = MemPtrNew(hdr + 4)) == NULL) return;
  memcpy(bmp, old, hdr);
  bmp[6] |= 0x10;                                  /* flags: indirect */
  bmp[hdr] = 0x06; bmp[hdr + 1] = 0x00; bmp[hdr + 2] = 0x00; bmp[hdr + 3] = 0x00;
  wh->bitmapP = (BitmapType *)bmp;
  bod_bmp_cache_flush();
  if ((uint32_t)BmpGetBits(wh->bitmapP) != 0x06000000) { *(uint32_t *)(bmp + hdr) = 0x06000000; bod_bmp_cache_flush(); }
  debug(DEBUG_INFO, "PLATFORM", "display bits at 0x%08X", (uint32_t)BmpGetBits(wh->bitmapP));
  MemPtrFree(old);
  WinEraseWindow();
}

/* The game changes palette entries at level load (WinPalette): mirror the
 * window's colour table into the hardware palette. */
void bod_palette_changed(ColorTableType *ct) {
  RGBColorType rgb;
  int i, n = CtbGetNumEntries(ct);
  if (n > 256) n = 256;
  for (i = 0; i < n; i++) {
    CtbGetEntry(ct, i, &rgb);
    PALRAM[i] = RGB15(rgb.r >> 3, rgb.g >> 3, rgb.b >> 3);
  }
  debug(DEBUG_INFO, "PLATFORM", "palette updated (%d entries)", n);
}

/* Same, but only when the table changed since last time (called per blit). */
void bod_palette_follow(ColorTableType *ct) {
  static uint32_t last[256];
  uint32_t *e = (uint32_t *)((uint8_t *)ct + 2);   /* entries follow the count */
  int i, n = CtbGetNumEntries(ct), changed = 0;
  if (n > 256) n = 256;
  for (i = 0; i < n; i++) if (e[i] != last[i]) { changed = 1; break; }
  if (!changed) return;
  for (i = 0; i < n; i++) last[i] = e[i];
  bod_palette_changed(ct);
}

/* A window whose bitmap is a region of the screen, for the menus: they are
 * drawn at a fixed place and there is no memory for a copy of the screen
 * behind them while a level is loaded. */
WinHandle bod_vram_window(Coord x, Coord y, Coord w, Coord h) {
  Err err;
  WinHandle wh = WinCreateOffscreenWindow(1, 1, nativeFormat, &err);
  BitmapType *bmp;
  RectangleType r;
  if (!wh) return NULL;
  if ((bmp = bod_vram_bitmap(x, y, w, h)) == NULL) { WinDeleteWindow(wh, false); return NULL; }
  BmpDelete(wh->bitmapP);
  wh->bitmapP = bmp;
  wh->windowBounds.topLeft.x = x; wh->windowBounds.topLeft.y = y;
  wh->windowBounds.extent.x = w; wh->windowBounds.extent.y = h;
  RctSetRectangle(&r, 0, 0, w, h);
  WinSetClipingBounds(wh, &r);
  bod_bmp_cache_flush();
  return wh;
}

/* A bitmap over the display's own bits: a form at (x,y) draws straight into
 * its place on the screen instead of into a buffer that would be copied
 * there (a full-screen form's buffer is 38 KB of a 256 KB machine). */
BitmapType *bod_vram_bitmap(Coord x, Coord y, Coord w, Coord h) {
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 240 || y + h > 160) return NULL;
  return bod_indirect_bitmap(w, h, 240, 8, false, (void *)(0x06000000 + y * 240 + x));
}

void platform_init(void) {
  RGBColorType *pal;
  heap_init();
  timer_init();
  display_init();
  debug_setsyslevel(NULL, DEBUG_INFO);
#ifdef BOD_TRACE
  { static char spec[] = BOD_TRACE; char *s = spec, *e; for (;;) { e = strchr(s, ','); if (e) *e = 0; debug_setsyslevel(s, DEBUG_TRACE); if (!e) break; s = e + 1; } }
#endif
  storage_init();
  {
    LocalID id = DmFindDatabase(0, "BOOT");
    if (!id || !DmOpenDatabase(0, id, dmModeReadOnly)) debug(DEBUG_ERROR, "PLATFORM", "no BOOT database");
  }
  storage_load();
#define STEP(x) do { uint32_t _f, _l, _u; debug(DEBUG_INFO, "PLATFORM", "init %s", #x); x; heap_stats(&_f, &_l, &_u); debug(DEBUG_INFO, "PLATFORM", "  -> chunks %u free %u", _u, _f); } while (0)
  STEP(SysUInitModule());
  STEP(PrefInitModule());
  STEP(UicInitModule());
  STEP(WinInitModule(kDensityLow, 240, 160, 8, false, NULL));
  STEP(pumpkin_load_fonts());
  STEP(FntInitModule(kDensityLow));
  STEP(FrmInitModule());
  STEP(InsPtInitModule());
  STEP(FldInitModule());
  STEP(MenuInitModule());
  STEP(EvtInitModule());
  STEP(SysInitModule());
  STEP(ClpInitModule());
  STEP(FtrInitModule());
  STEP(KeyInitModule());
  STEP(CharAttrInitModule());
  pal = WinGetPalette(256);
  if (pal) screen_set_palette(pal, 256);
  display_to_vram();
  {
    uint32_t f, l, u;
    heap_stats(&f, &l, &u);
    debug(DEBUG_INFO, "PLATFORM", "up: heap free %u largest %u chunks %u", f, l, u);
    heap_histogram();
  }
}
