/* The Data Manager and Memory Manager on the GBA.
 *
 * Databases come from two places: the ROM data image, read in place, and a
 * small set of databases the game creates (profiles, results), which live in
 * the heap and are mirrored into the cartridge SRAM. Resources of a few types
 * are decoded into host structures on demand, as PumpkinOS does. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "mutex.h"
#include "storage.h"
#include "bod.h"
#include "debug.h"
#include "heap.h"
#include "romimage.h"
#include "libc.h"
#include "gba.h"

#define MAX_OPEN 24
#define MAX_RAMDB 32
#define MAX_DECODED 48

typedef struct ram_db {
  char name[32];
  uint32_t type, creator;
  uint16_t attrs, version;
  uint32_t crDate, modDate, seed;
  uint16_t nrecs, cap;
  uint32_t **recs;          /* masters, i.e. handles */
  uint8_t is_res, dirty, used;
} ram_db_t;

typedef struct {
  uint8_t used, is_ram, is_res;
  uint16_t mode;
  const rom_db_t *rom;
  ram_db_t *ram;
  uint16_t count;           /* open count */
} open_db_t;

typedef struct { MemHandle h; void *decoded; uint16_t use; uint16_t type_hi; void (*destroy)(void *); } decoded_t;

static open_db_t opens[MAX_OPEN];
static ram_db_t ramdbs[MAX_RAMDB];
static decoded_t decoded[MAX_DECODED];
static open_db_t *res_order[MAX_OPEN];   /* open resource databases, most recent first */
static int nres_order;
static Err last_err;
static const rom_image_t *image;
static DmOpenRef app_db;

/* ---- helpers ---- */

static inline chunk_t *hchunk(MemHandle h) { return CHUNK_OF(*(uint32_t *)h); }
static inline MemHandle rom_handle(uint32_t chunk_addr) { return (MemHandle)chunk_addr; }   /* the master word is first */

static int is_ram_id(LocalID id) { return (uint8_t *)id >= (uint8_t *)ramdbs && (uint8_t *)id < (uint8_t *)(ramdbs + MAX_RAMDB); }
static int is_rom_id(LocalID id) { return id >= ROMIMAGE_BASE; }

static open_db_t *ref(DmOpenRef r) {
  open_db_t *o = (open_db_t *)r;
  if (o >= opens && o < opens + MAX_OPEN && o->used) return o;
  return NULL;
}

static uint32_t db_count(open_db_t *o) { return o->is_ram ? o->ram->nrecs : o->rom->nentries; }

static MemHandle db_handle(open_db_t *o, uint16_t index) {
  if (index >= db_count(o)) return NULL;
  if (o->is_ram) return (MemHandle)o->ram->recs[index];
  if (o->is_res) return rom_handle(((const rom_res_t *)o->rom->entries)[index].chunk);
  return rom_handle(((const rom_rec_t *)o->rom->entries)[index].chunk);
}

void storage_init(void) {
  image = (const rom_image_t *)ROMIMAGE_BASE;
  if (image->magic != ROMIMAGE_MAGIC) {
    debug(DEBUG_ERROR, "STOR", "no data image at 0x%08X (magic 0x%08X)", ROMIMAGE_BASE, image->magic);
    image = NULL;
    return;
  }
  debug(DEBUG_INFO, "STOR", "data image: %u databases", image->ndbs);
}

static const rom_db_t *rom_find_name(const char *name) {
  uint32_t i;
  if (!image) return NULL;
  for (i = 0; i < image->ndbs; i++) if (!strcmp(image->dbs[i].name, name)) return &image->dbs[i];
  return NULL;
}

static ram_db_t *ram_find_name(const char *name) {
  int i;
  for (i = 0; i < MAX_RAMDB; i++) if (ramdbs[i].used && !strcmp(ramdbs[i].name, name)) return &ramdbs[i];
  return NULL;
}

/* ---- decoded resources ---- */

static void *decode_resource(MemHandle h, uint32_t type, uint16_t id, void **raw, uint32_t size, void (**destroy)(void *)) {
  uint32_t dsize;
  uint8_t *p = *raw;
  void *d = NULL;
  *destroy = NULL;
  switch (type) {
    case alertRscType:   d = pumpkin_create_alert(h, p, &dsize); *destroy = pumpkin_destroy_alert; break;
    case MenuRscType:    d = pumpkin_create_menu(h, p, &dsize); *destroy = pumpkin_destroy_menu; break;
    case fontRscType:    d = pumpkin_create_font(h, p, size, &dsize); *destroy = pumpkin_destroy_font; break;
    case fontExtRscType: d = pumpkin_create_fontv2(h, p, size, &dsize); *destroy = pumpkin_destroy_fontv2; break;
    case constantRscType:
      if (size == 4) { uint8_t *a = heap_alloc(4); a[0] = p[3]; a[1] = p[2]; a[2] = p[1]; a[3] = p[0]; d = a; *destroy = heap_free; }
      break;
    default: break;
  }
  (void)id;
  return d;
}

static decoded_t *decoded_find(MemHandle h) {
  int i;
  for (i = 0; i < MAX_DECODED; i++) if (decoded[i].h == h) return &decoded[i];
  return NULL;
}

static decoded_t *decoded_get(MemHandle h) {
  chunk_t *c = hchunk(h);
  decoded_t *d;
  int i;
  if (!(c->flags & CF_RES)) return NULL;
  switch (c->type) {
    case alertRscType: case MenuRscType: case fontRscType: case fontExtRscType: case constantRscType: break;
    default: return NULL;
  }
  if ((d = decoded_find(h)) != NULL) return d;
  for (i = 0; i < MAX_DECODED; i++) if (!decoded[i].h) break;
  if (i == MAX_DECODED) { debug(DEBUG_ERROR, "STOR", "decoded table full"); return NULL; }
  d = &decoded[i];
  d->h = h;
  d->use = 0;
  {
    void *raw = (void *)*(uint32_t *)h;
    d->decoded = decode_resource(h, c->type, c->id, &raw, c->size, &d->destroy);
  }
  if (!d->decoded) { d->h = NULL; return NULL; }
  return d;
}

static void decoded_release(MemHandle h) {
  decoded_t *d = decoded_find(h);
  if (!d) return;
  if (d->use) d->use--;
  if (d->use == 0 && hchunk(h)->lock == 0) {
    if (d->destroy) d->destroy(d->decoded);
    d->h = NULL; d->decoded = NULL;
  }
}

/* ---- Memory Manager ---- */

Err MemInit(void) { return errNone; }

MemHandle MemHandleNew(UInt32 size) {
  void *p = heap_alloc(size);
  if (!p) { debug(DEBUG_ERROR, "STOR", "MemHandleNew(%u) failed", size); return NULL; }
  return (MemHandle)CHUNK_OF(p)->master;
}

MemPtr MemPtrNew(UInt32 size) {
  void *p = heap_alloc(size);
  if (!p) { debug(DEBUG_ERROR, "STOR", "MemPtrNew(%u) failed", size); return NULL; }
  CHUNK_OF(p)->lock = 1;
  return p;
}

MemPtr MemChunkNew(UInt16 heapID, UInt32 size, UInt16 attr) {
  (void)heapID;
  if (attr & memNewChunkFlagNonMovable) return MemPtrNew(size);
  return (MemPtr)MemHandleNew(size);
}

MemPtr MemHandleLockEx(MemHandle h, Boolean dec) {
  chunk_t *c;
  decoded_t *d;
  if (!h) return NULL;
  c = hchunk(h);
  if (c->magic != CHUNK_MAGIC) { debug(DEBUG_ERROR, "STOR", "MemHandleLock bad handle %p", h); return NULL; }
  if (!IS_ROM(c) && c->lock < 15) c->lock++;
  if (dec && (c->flags & CF_RES) && (d = decoded_get(h)) != NULL) return d->decoded;
  return (MemPtr)*(uint32_t *)h;
}

MemPtr MemHandleLock(MemHandle h) { return MemHandleLockEx(h, true); }

Err MemHandleUnlockEx(MemHandle h, UInt16 *lockCount) {
  chunk_t *c;
  if (!h) return memErrInvalidParam;
  c = hchunk(h);
  if (!IS_ROM(c) && c->lock > 0) c->lock--;
  if (lockCount) *lockCount = c->lock;
  return errNone;
}

Err MemHandleUnlock(MemHandle h) { return MemHandleUnlockEx(h, NULL); }

Err MemPtrUnlock(MemPtr p) {
  chunk_t *c;
  if (!p) return memErrInvalidParam;
  c = CHUNK_OF(p);
  if (!IS_ROM(c) && c->lock > 0) c->lock--;
  return errNone;
}

Err MemHandleFree(MemHandle h) {
  if (!h) return memErrInvalidParam;
  if (IS_ROM(h)) return errNone;
  {
    decoded_t *d = decoded_find(h);
    if (d) { if (d->destroy) d->destroy(d->decoded); d->h = NULL; d->decoded = NULL; }
  }
  heap_free((void *)*(uint32_t *)h);
  return errNone;
}

Err MemChunkFree(MemPtr p) {
  chunk_t *c;
  if (!p) return memErrInvalidParam;
  if (IS_ROM(p)) return errNone;
  c = CHUNK_OF(p);
  if (c->magic != CHUNK_MAGIC) { extern uint32_t arm_syscall_lr; debug(DEBUG_ERROR, "STOR", "MemChunkFree bad pointer %p (trap 0x%04X, arm lr 0x%08X)", p, heap_trap, arm_syscall_lr); return memErrInvalidParam; }
  return MemHandleFree((MemHandle)c->master);
}


MemHandle MemPtrRecoverHandle(MemPtr p) {
  chunk_t *c;
  if (!p) return NULL;
  c = CHUNK_OF(p);
  if (c->magic != CHUNK_MAGIC) { debug(DEBUG_ERROR, "STOR", "MemPtrRecoverHandle bad pointer %p", p); return NULL; }
  return (MemHandle)c->master;
}

UInt32 MemHandleSize(MemHandle h) { return h ? hchunk(h)->size : 0; }
UInt32 MemPtrSize(MemPtr p) { return p ? CHUNK_OF(p)->size : 0; }

Err MemHandleResize(MemHandle h, UInt32 newSize) {
  void *p;
  if (!h || IS_ROM(h)) return memErrInvalidParam;
  p = heap_realloc((void *)*(uint32_t *)h, newSize);
  return p ? errNone : memErrNotEnoughSpace;
}

Err MemPtrResize(MemPtr p, UInt32 newSize) {
  void *q;
  if (!p || IS_ROM(p)) return memErrInvalidParam;
  q = heap_realloc(p, newSize);
  if (q != p) {
    /* a locked chunk moved: only its handle knows the new address */
    debug(DEBUG_ERROR, "STOR", "MemPtrResize(%p, %u) moved the chunk", p, newSize);
    return memErrChunkNotLocked;
  }
  return q ? errNone : memErrNotEnoughSpace;
}

UInt16 MemHandleLockCount(MemHandle h) { return h ? hchunk(h)->lock : 0; }
Boolean MemHandleDataStorage(MemHandle h) { return h && IS_ROM(h); }
Boolean MemPtrDataStorage(MemPtr p) { return p && IS_ROM(p); }
Err MemHandleSetOwner(MemHandle h, UInt16 owner) { (void)h; (void)owner; return errNone; }
Err MemPtrSetOwner(MemPtr p, UInt16 owner) { (void)p; (void)owner; return errNone; }
UInt16 MemPtrOwner(MemPtr p) { (void)p; return 0; }
UInt16 MemHandleOwner(MemHandle h) { (void)h; return 0; }
UInt16 MemPtrHeapID(MemPtr p) { (void)p; return 0; }
UInt16 MemHandleHeapID(MemHandle h) { (void)h; return 0; }
UInt16 MemPtrCardNo(MemPtr p) { (void)p; return 0; }
UInt16 MemHandleCardNo(MemHandle h) { (void)h; return 0; }
Err MemPtrResetLock(MemPtr p) { if (p && !IS_ROM(p)) CHUNK_OF(p)->lock = 0; return errNone; }
Err MemHandleResetLock(MemHandle h) { if (h && !IS_ROM(h)) hchunk(h)->lock = 0; return errNone; }
UInt16 MemPtrFlags(MemPtr p) { (void)p; return 0; }
UInt16 MemHandleFlags(MemHandle h) { (void)h; return 0; }
LocalID MemPtrToLocalID(MemPtr p) { return (LocalID)p; }
LocalID MemHandleToLocalID(MemHandle h) { return (LocalID)h; }
MemHandle MemLocalIDToHandle(LocalID local) { return (MemHandle)local; }
MemPtr MemLocalIDToPtr(LocalID local, UInt16 cardNo) { (void)cardNo; return (MemPtr)local; }
MemPtr MemLocalIDToLockedPtr(LocalID local, UInt16 cardNo) { (void)cardNo; return MemHandleLock((MemHandle)local); }
MemPtr MemLocalIDToGlobal(LocalID local, UInt16 cardNo) { (void)cardNo; return (MemPtr)local; }
LocalIDKind MemLocalIDKind(LocalID local) { (void)local; return memIDHandle; }
UInt16 MemNumCards(void) { return 1; }
UInt16 MemNumHeaps(UInt16 cardNo) { (void)cardNo; return 1; }
UInt16 MemNumRAMHeaps(UInt16 cardNo) { (void)cardNo; return 1; }
UInt16 MemHeapID(UInt16 cardNo, UInt16 heapIndex) { (void)cardNo; (void)heapIndex; return 0; }
Boolean MemHeapDynamic(UInt16 heapID) { (void)heapID; return true; }
UInt16 MemHeapFlags(UInt16 heapID) { (void)heapID; return 0; }
UInt32 MemHeapSize(UInt16 heapID) { (void)heapID; return 256 * 1024; }
Err MemHeapCompact(UInt16 heapID) { (void)heapID; return errNone; }
Err MemHeapCheck(UInt16 heapID) { (void)heapID; return heap_check("MemHeapCheck") ? memErrChunkLocked : errNone; }
Err MemHeapScramble(UInt16 heapID) { (void)heapID; return errNone; }
Err MemHeapFreeByOwnerID(UInt16 heapID, UInt16 ownerID) { (void)heapID; (void)ownerID; return errNone; }
Err MemHeapInit(UInt16 heapID, Int16 numHandles, Boolean initContents) { (void)heapID; (void)numHandles; (void)initContents; return errNone; }
Err MemKernelInit(void) { return errNone; }
Err MemInitHeapTable(UInt16 cardNo) { (void)cardNo; return errNone; }
Err MemSemaphoreReserve(Boolean writeAccess) { (void)writeAccess; return errNone; }
Err MemSemaphoreRelease(Boolean writeAccess) { (void)writeAccess; return errNone; }
UInt16 MemDebugMode(void) { return 0; }
Err MemSetDebugMode(UInt16 flags) { (void)flags; return errNone; }

/* The game asks how much heap is free and, below a megabyte, drops its
 * background layer and moves buffers into feature memory. Report a roomy
 * Palm: the buffers it then keeps come from ROM anyway (texhook.c). */
Err MemHeapFreeBytes(UInt16 heapID, UInt32 *freeP, UInt32 *maxP) {
  (void)heapID;
  if (freeP) *freeP = 4 * 1024 * 1024;
  if (maxP) *maxP = 3 * 1024 * 1024;
  return errNone;
}

Err MemCardInfo(UInt16 cardNo, Char *cardNameP, Char *manufNameP, UInt16 *versionP, UInt32 *crDateP, UInt32 *romSizeP, UInt32 *ramSizeP, UInt32 *freeBytesP) {
  uint32_t f, l, u;
  (void)cardNo;
  heap_stats(&f, &l, &u);
  if (cardNameP) strcpy(cardNameP, "GBA");
  if (manufNameP) strcpy(manufNameP, "Nintendo");
  if (versionP) *versionP = 1;
  if (crDateP) *crDateP = 0;
  if (romSizeP) *romSizeP = 32 * 1024 * 1024;
  if (ramSizeP) *ramSizeP = 256 * 1024;
  if (freeBytesP) *freeBytesP = f;
  return errNone;
}

static void vram_move(uint8_t *d, const uint8_t *s, uint32_t n) {
  /* VRAM ignores byte writes: do the ends as halfword read-modify-write */
  if (n >= 64 && (s + n <= d || d + n <= s)) {
    /* a whole-window blit: let DMA do the bulk (bitmap bits sit 2 bytes past a word boundary) */
    if (!(((uint32_t)d | (uint32_t)s) & 3)) {
      uint32_t words = n / 4;
      dma3_copy32(d, s, words);
      d += words * 4; s += words * 4; n -= words * 4;
    } else if (!(((uint32_t)d | (uint32_t)s) & 1)) {
      uint32_t hw = n / 2;
      while (hw) { uint32_t k = hw > 0x3FFF ? 0x3FFF : hw; dma3_copy16(d, s, k); d += k * 2; s += k * 2; hw -= k; n -= k * 2; }
    }
    if (!n) return;
  }
  while (n && ((uint32_t)d & 1)) { vu16 *hw = (vu16 *)(d - 1); *hw = (u16)((*hw & 0x00FF) | (*s << 8)); d++; s++; n--; }
  if ((uint32_t)s & 1) { while (n >= 2) { *(vu16 *)d = (u16)(s[0] | (s[1] << 8)); d += 2; s += 2; n -= 2; } }
  else { while (n >= 2) { *(vu16 *)d = *(const u16 *)s; d += 2; s += 2; n -= 2; } }
  if (n) { vu16 *hw = (vu16 *)d; *hw = (u16)((*hw & 0xFF00) | *s); }
}
static void vram_set(uint8_t *d, uint8_t v, uint32_t n) {
  u16 vv = (u16)(v | (v << 8));
  if (n && ((uint32_t)d & 1)) { vu16 *hw = (vu16 *)(d - 1); *hw = (u16)((*hw & 0x00FF) | (v << 8)); d++; n--; }
  while (n >= 2) { *(vu16 *)d = vv; d += 2; n -= 2; }
  if (n) { vu16 *hw = (vu16 *)d; *hw = (u16)((*hw & 0xFF00) | v); }
}
#define IN_VRAM(p) ((((uint32_t)(p)) >> 24) == 0x06)

uint32_t bod_mm_bytes[4];   /* [0] 68k->RAM [1] 68k->VRAM [2] ARM->RAM [3] ARM->VRAM */
int bod_mm_arm;
Err MemMove(void *dstP, const void *sP, Int32 numBytes) {
  if (dstP && sP && numBytes > 0 && !IS_ROM(dstP)) {
    bod_mm_bytes[(bod_mm_arm ? 2 : 0) + (IN_VRAM(dstP) ? 1 : 0)] += numBytes;
    if (!bod_mm_arm && IN_VRAM(dstP)) { extern uint32_t bod_mm_rows[4]; static int n; bod_mm_rows[numBytes >= 240 ? 0 : numBytes >= 160 ? 1 : numBytes >= 64 ? 2 : 3]++;
      (void)n; }
    if (IN_VRAM(dstP)) vram_move(dstP, sP, numBytes); else memmove(dstP, sP, numBytes);
  }
  return errNone;
}
Err MemSet(void *dstP, Int32 numBytes, UInt8 value) {
  if (dstP && numBytes > 0 && !IS_ROM(dstP)) {
    if (IN_VRAM(dstP)) vram_set(dstP, value, numBytes); else memset(dstP, value, numBytes);
  }
  return errNone;
}
Int16 MemCmp(const void *s1, const void *s2, Int32 numBytes) { return (Int16)memcmp(s1, s2, numBytes); }

/* ---- Data Manager: databases ---- */

Err DmGetLastErr(void) { return last_err; }
Err DmInit(void) { return errNone; }
void DmSync(void) {}

static open_db_t *open_alloc(void) {
  int i;
  for (i = 0; i < MAX_OPEN; i++) if (!opens[i].used) { memset(&opens[i], 0, sizeof(open_db_t)); opens[i].used = 1; return &opens[i]; }
  return NULL;
}

static void res_order_add(open_db_t *o) {
  int i;
  for (i = 0; i < nres_order; i++) if (res_order[i] == o) return;
  if (nres_order < MAX_OPEN) { memmove(&res_order[1], &res_order[0], nres_order * sizeof(open_db_t *)); res_order[0] = o; nres_order++; }
}

static void res_order_remove(open_db_t *o) {
  int i, j;
  for (i = j = 0; i < nres_order; i++) if (res_order[i] != o) res_order[j++] = res_order[i];
  nres_order = j;
}

DmOpenRef DmOpenDatabase(UInt16 cardNo, LocalID dbID, UInt16 mode) {
  open_db_t *o;
  int i;
  (void)cardNo;
  if (!dbID) { last_err = dmErrCantFind; return NULL; }
  /* an already open database is shared */
  for (i = 0; i < MAX_OPEN; i++) {
    o = &opens[i];
    if (o->used && ((o->is_ram && (LocalID)o->ram == dbID) || (!o->is_ram && (LocalID)o->rom == dbID))) {
      o->count++;
      o->mode |= mode;
      return (DmOpenRef)o;
    }
  }
  if ((o = open_alloc()) == NULL) { last_err = dmErrCantOpen; return NULL; }
  o->mode = mode;
  o->count = 1;
  if (is_ram_id(dbID)) {
    o->is_ram = 1; o->ram = (ram_db_t *)dbID; o->is_res = o->ram->is_res;
  } else if (is_rom_id(dbID)) {
    o->rom = (const rom_db_t *)dbID; o->is_res = (o->rom->flags & ROMDB_RESOURCE) != 0;
  } else {
    o->used = 0; last_err = dmErrCantFind; return NULL;
  }
  if (o->is_res) res_order_add(o);
  last_err = errNone;
  return (DmOpenRef)o;
}

Err DmCloseDatabase(DmOpenRef dbP) {
  open_db_t *o = ref(dbP);
  if (!o) return dmErrInvalidParam;
  if (o->count > 1) { o->count--; return errNone; }
  if (o == (open_db_t *)app_db) { o->count = 1; return errNone; }  /* the application's own database stays open */
  if (o->is_res) res_order_remove(o);
  if (o->is_ram && o->ram->dirty) storage_sync();
  o->used = 0;
  return errNone;
}

Err DmOpenDatabaseInfo(DmOpenRef dbP, LocalID *dbIDP, UInt16 *openCountP, UInt16 *modeP, UInt16 *cardNoP, Boolean *resDBP) {
  open_db_t *o = ref(dbP);
  if (!o) return dmErrInvalidParam;
  if (dbIDP) *dbIDP = o->is_ram ? (LocalID)o->ram : (LocalID)o->rom;
  if (openCountP) *openCountP = o->count;
  if (modeP) *modeP = o->mode;
  if (cardNoP) *cardNoP = 0;
  if (resDBP) *resDBP = o->is_res;
  return errNone;
}

LocalID DmFindDatabase(UInt16 cardNo, const Char *nameP) {
  ram_db_t *r;
  const rom_db_t *d;
  (void)cardNo;
  if (!nameP) return 0;
  if ((r = ram_find_name(nameP)) != NULL) return (LocalID)r;
  if ((d = rom_find_name(nameP)) != NULL) return (LocalID)d;
  last_err = dmErrCantFind;
  return 0;
}

Err DmDatabaseInfo(UInt16 cardNo, LocalID dbID, Char *nameP, UInt16 *attributesP, UInt16 *versionP, UInt32 *crDateP, UInt32 *modDateP, UInt32 *bckUpDateP, UInt32 *modNumP, LocalID *appInfoIDP, LocalID *sortInfoIDP, UInt32 *typeP, UInt32 *creatorP) {
  (void)cardNo;
  if (is_ram_id(dbID)) {
    ram_db_t *r = (ram_db_t *)dbID;
    if (nameP) strcpy(nameP, r->name);
    if (attributesP) *attributesP = r->attrs;
    if (versionP) *versionP = r->version;
    if (crDateP) *crDateP = r->crDate;
    if (modDateP) *modDateP = r->modDate;
    if (bckUpDateP) *bckUpDateP = 0;
    if (modNumP) *modNumP = 0;
    if (appInfoIDP) *appInfoIDP = 0;
    if (sortInfoIDP) *sortInfoIDP = 0;
    if (typeP) *typeP = r->type;
    if (creatorP) *creatorP = r->creator;
    return errNone;
  }
  if (is_rom_id(dbID)) {
    const rom_db_t *d = (const rom_db_t *)dbID;
    if (nameP) strcpy(nameP, d->name);
    if (attributesP) *attributesP = d->attrs;
    if (versionP) *versionP = d->version;
    if (crDateP) *crDateP = d->crDate;
    if (modDateP) *modDateP = d->modDate;
    if (bckUpDateP) *bckUpDateP = 0;
    if (modNumP) *modNumP = 0;
    if (appInfoIDP) *appInfoIDP = d->appinfo;
    if (sortInfoIDP) *sortInfoIDP = 0;
    if (typeP) *typeP = d->type;
    if (creatorP) *creatorP = d->creator;
    return errNone;
  }
  return dmErrInvalidParam;
}

Err DmSetDatabaseInfo(UInt16 cardNo, LocalID dbID, const Char *nameP, UInt16 *attributesP, UInt16 *versionP, UInt32 *crDateP, UInt32 *modDateP, UInt32 *bckUpDateP, UInt32 *modNumP, LocalID *appInfoIDP, LocalID *sortInfoIDP, UInt32 *typeP, UInt32 *creatorP) {
  (void)cardNo; (void)bckUpDateP; (void)modNumP; (void)appInfoIDP; (void)sortInfoIDP;
  if (is_ram_id(dbID)) {
    ram_db_t *r = (ram_db_t *)dbID;
    if (nameP) strncpy(r->name, nameP, 31);
    if (attributesP) r->attrs = *attributesP;
    if (versionP) r->version = *versionP;
    if (crDateP) r->crDate = *crDateP;
    if (modDateP) r->modDate = *modDateP;
    if (typeP) r->type = *typeP;
    if (creatorP) r->creator = *creatorP;
    r->dirty = 1;
    return errNone;
  }
  debug(DEBUG_INFO, "STOR", "DmSetDatabaseInfo on a ROM database ignored");
  return dmErrReadOnly;
}

Err DmDatabaseSize(UInt16 cardNo, LocalID dbID, UInt32 *numRecordsP, UInt32 *totalBytesP, UInt32 *dataBytesP) {
  uint32_t n = 0, bytes = 0, i;
  (void)cardNo;
  if (is_ram_id(dbID)) {
    ram_db_t *r = (ram_db_t *)dbID;
    n = r->nrecs;
    for (i = 0; i < n; i++) bytes += heap_size((void *)*r->recs[i]);
  } else if (is_rom_id(dbID)) {
    const rom_db_t *d = (const rom_db_t *)dbID;
    n = d->nentries;
    for (i = 0; i < n; i++) {
      uint32_t chunk = (d->flags & ROMDB_RESOURCE) ? ((const rom_res_t *)d->entries)[i].chunk : ((const rom_rec_t *)d->entries)[i].chunk;
      bytes += ((const chunk_t *)chunk)->size;
    }
  } else return dmErrInvalidParam;
  if (numRecordsP) *numRecordsP = n;
  if (totalBytesP) *totalBytesP = bytes + 128 + n * 8;
  if (dataBytesP) *dataBytesP = bytes;
  return errNone;
}

Err DmCreateDatabase(UInt16 cardNo, const Char *nameP, UInt32 creator, UInt32 type, Boolean resDB) {
  int i;
  (void)cardNo;
  if (!nameP) return dmErrInvalidParam;
  if (ram_find_name(nameP) || rom_find_name(nameP)) return dmErrAlreadyExists;
  for (i = 0; i < MAX_RAMDB; i++) if (!ramdbs[i].used) break;
  if (i == MAX_RAMDB) return dmErrMemError;
  memset(&ramdbs[i], 0, sizeof(ram_db_t));
  ramdbs[i].used = 1;
  strncpy(ramdbs[i].name, nameP, 31);
  ramdbs[i].creator = creator;
  ramdbs[i].type = type;
  ramdbs[i].is_res = resDB;
  ramdbs[i].attrs = resDB ? dmHdrAttrResDB : 0;
  ramdbs[i].crDate = ramdbs[i].modDate = TimGetSeconds();
  ramdbs[i].seed = 1;
  ramdbs[i].dirty = 1;
  debug(DEBUG_INFO, "STOR", "DmCreateDatabase \"%s\"", nameP);
  return errNone;
}

Err DmCreateDatabaseEx(const Char *nameP, UInt32 creator, UInt32 type, UInt16 attr, UInt32 uniqueIDSeed, Boolean overwrite) {
  Err err;
  (void)uniqueIDSeed;
  if (overwrite) { LocalID id = DmFindDatabase(0, nameP); if (id && is_ram_id(id)) DmDeleteDatabase(0, id); }
  err = DmCreateDatabase(0, nameP, creator, type, (attr & dmHdrAttrResDB) != 0);
  if (!err) { ram_db_t *r = ram_find_name(nameP); if (r) r->attrs = attr; }
  return err;
}

Err DmDeleteDatabase(UInt16 cardNo, LocalID dbID) {
  ram_db_t *r;
  int i;
  (void)cardNo;
  if (!is_ram_id(dbID)) return dmErrReadOnly;
  r = (ram_db_t *)dbID;
  for (i = 0; i < MAX_OPEN; i++) if (opens[i].used && opens[i].is_ram && opens[i].ram == r) return dmErrDatabaseOpen;
  for (i = 0; i < r->nrecs; i++) MemHandleFree((MemHandle)r->recs[i]);
  if (r->recs) heap_free(r->recs);
  r->used = 0;
  storage_sync();
  return errNone;
}

DmOpenRef DmOpenDatabaseByTypeCreator(UInt32 type, UInt32 creator, UInt16 mode) {
  DmSearchStateType st;
  UInt16 card;
  LocalID id;
  if (DmGetNextDatabaseByTypeCreator(true, &st, type, creator, true, &card, &id) == errNone) return DmOpenDatabase(card, id, mode);
  return NULL;
}

DmOpenRef DmOpenDBNoOverlay(UInt16 cardNo, LocalID dbID, UInt16 mode) { return DmOpenDatabase(cardNo, dbID, mode); }

Err DmGetNextDatabaseByTypeCreator(Boolean newSearch, DmSearchStatePtr stateInfoP, UInt32 type, UInt32 creator, Boolean onlyLatestVers, UInt16 *cardNoP, LocalID *dbIDP) {
  uint32_t i;
  (void)onlyLatestVers;
  if (newSearch) stateInfoP->info[0] = 0;
  i = stateInfoP->info[0];
  /* RAM databases first, then the image */
  for (; i < MAX_RAMDB; i++) {
    ram_db_t *r = &ramdbs[i];
    if (r->used && (!type || r->type == type) && (!creator || r->creator == creator)) {
      stateInfoP->info[0] = i + 1;
      if (cardNoP) *cardNoP = 0;
      if (dbIDP) *dbIDP = (LocalID)r;
      return errNone;
    }
  }
  if (image) {
    for (; i - MAX_RAMDB < image->ndbs; i++) {
      const rom_db_t *d = &image->dbs[i - MAX_RAMDB];
      if ((!type || d->type == type) && (!creator || d->creator == creator)) {
        stateInfoP->info[0] = i + 1;
        if (cardNoP) *cardNoP = 0;
        if (dbIDP) *dbIDP = (LocalID)d;
        return errNone;
      }
    }
  }
  stateInfoP->info[0] = i;
  return dmErrCantFind;
}

Err DmGetNextDatabaseByTypeCreatorEx(Boolean newSearch, DmSearchStatePtr stateInfoP, UInt32 type, UInt32 creator, Boolean onlyLatestVers, UInt16 *cardNoP, LocalID *dbIDP) {
  return DmGetNextDatabaseByTypeCreator(newSearch, stateInfoP, type, creator, onlyLatestVers, cardNoP, dbIDP);
}

UInt16 DmNumDatabases(UInt16 cardNo) { int i, n = 0; (void)cardNo; for (i = 0; i < MAX_RAMDB; i++) if (ramdbs[i].used) n++; return n + (image ? image->ndbs : 0); }

LocalID DmGetDatabase(UInt16 cardNo, UInt16 index) {
  int i, n = 0;
  (void)cardNo;
  for (i = 0; i < MAX_RAMDB; i++) if (ramdbs[i].used) { if (n == index) return (LocalID)&ramdbs[i]; n++; }
  if (image && index - n < image->ndbs) return (LocalID)&image->dbs[index - n];
  return 0;
}

DmOpenRef DmNextOpenDatabase(DmOpenRef currentP) {
  int i = currentP ? (int)((open_db_t *)currentP - opens) + 1 : 0;
  for (; i < MAX_OPEN; i++) if (opens[i].used) return (DmOpenRef)&opens[i];
  return NULL;
}

DmOpenRef DmNextOpenResDatabase(DmOpenRef dbP) {
  int i;
  if (!dbP) return nres_order ? (DmOpenRef)res_order[0] : NULL;
  for (i = 0; i < nres_order; i++) if (res_order[i] == (open_db_t *)dbP) return (i + 1 < nres_order) ? (DmOpenRef)res_order[i + 1] : NULL;
  return NULL;
}

LocalID DmGetAppInfoID(DmOpenRef dbP) {
  open_db_t *o = ref(dbP);
  if (!o || o->is_ram) return 0;
  return o->rom->appinfo;
}

Err DmDatabaseProtect(UInt16 cardNo, LocalID dbID, Boolean protect) { (void)cardNo; (void)dbID; (void)protect; return errNone; }
void DmGetDatabaseLockState(DmOpenRef dbR, UInt8 *highest, UInt32 *count, UInt32 *busy) { (void)dbR; if (highest) *highest = 0; if (count) *count = 0; if (busy) *busy = 0; }
Err DmSyncDatabase(DmOpenRef dbRef) { (void)dbRef; storage_sync(); return errNone; }
Err DmSetDirty(MemHandle handle) { (void)handle; return errNone; }

void storage_set_app_db(DmOpenRef db) { app_db = db; }
DmOpenRef storage_app_db(void) { return app_db; }

/* ---- resources ---- */

static int res_find(open_db_t *o, uint32_t type, uint16_t id) {
  uint32_t i, n = db_count(o);
  if (o->is_ram) {
    for (i = 0; i < n; i++) { chunk_t *c = hchunk((MemHandle)o->ram->recs[i]); if (c->type == type && c->id == id) return (int)i; }
  } else {
    const rom_res_t *e = (const rom_res_t *)o->rom->entries;
    for (i = 0; i < n; i++) if (e[i].type == type && e[i].id == id) return (int)i;
  }
  return -1;
}

static MemHandle res_get(open_db_t *o, uint32_t type, uint16_t id) {
  int i = res_find(o, type, id);
  return i < 0 ? NULL : db_handle(o, (uint16_t)i);
}

static MemHandle res_use(MemHandle h) {
  decoded_t *d;
  if (h && (d = decoded_get(h)) != NULL) d->use++;
  return h;
}

MemHandle DmGetResource(DmResType type, DmResID resID) {
  int i;
  MemHandle h;
  for (i = 0; i < nres_order; i++) {
    if ((h = res_get(res_order[i], type, resID)) != NULL) return res_use(h);
  }
  last_err = dmErrResourceNotFound;
  return NULL;
}

MemHandle DmGet1Resource(DmResType type, DmResID resID) {
  MemHandle h;
  if (nres_order && (h = res_get(res_order[0], type, resID)) != NULL) return res_use(h);
  last_err = dmErrResourceNotFound;
  return NULL;
}

MemHandle DmGetResourceDecoded(DmResType type, DmResID resID) { return DmGetResource(type, resID); }

Err DmReleaseResource(MemHandle resourceH) {
  if (!resourceH) return dmErrInvalidParam;
  decoded_release(resourceH);
  return errNone;
}

UInt16 DmFindResource(DmOpenRef dbP, DmResType resType, DmResID resID, MemHandle resH) {
  open_db_t *o = ref(dbP);
  uint32_t i, n;
  if (!o || !o->is_res) return 0xFFFF;
  if (resH) {
    n = db_count(o);
    for (i = 0; i < n; i++) if (db_handle(o, (uint16_t)i) == resH) return (UInt16)i;
    return 0xFFFF;
  }
  i = res_find(o, resType, resID);
  return i < 0 ? 0xFFFF : (UInt16)i;
}

UInt16 DmFindResourceType(DmOpenRef dbP, DmResType resType, UInt16 typeIndex) {
  open_db_t *o = ref(dbP);
  uint32_t i, n, k = 0;
  if (!o || !o->is_res) return 0xFFFF;
  n = db_count(o);
  for (i = 0; i < n; i++) {
    uint32_t t = o->is_ram ? hchunk((MemHandle)o->ram->recs[i])->type : ((const rom_res_t *)o->rom->entries)[i].type;
    if (t == resType) { if (k == typeIndex) return (UInt16)i; k++; }
  }
  return 0xFFFF;
}

MemHandle DmGetResourceIndex(DmOpenRef dbP, UInt16 index) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || !o->is_res) return NULL;
  h = db_handle(o, index);
  return res_use(h);
}

UInt16 DmNumResources(DmOpenRef dbP) { open_db_t *o = ref(dbP); return o ? (UInt16)db_count(o) : 0; }

Err DmResourceInfo(DmOpenRef dbP, UInt16 index, DmResType *resTypeP, DmResID *resIDP, LocalID *chunkLocalIDP) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || !o->is_res) return dmErrInvalidParam;
  if ((h = db_handle(o, index)) == NULL) return dmErrIndexOutOfRange;
  if (resTypeP) *resTypeP = hchunk(h)->type;
  if (resIDP) *resIDP = hchunk(h)->id;
  if (chunkLocalIDP) *chunkLocalIDP = (LocalID)h;
  return errNone;
}

Err DmResourceType(MemHandle h, DmResType *resType, DmResID *resID) {
  if (!h) return dmErrInvalidParam;
  if (resType) *resType = hchunk(h)->type;
  if (resID) *resID = hchunk(h)->id;
  return errNone;
}

Err DmSearchResource(DmResType resType, DmResID resID, MemHandle resH, DmOpenRef *dbPP) {
  int i;
  for (i = 0; i < nres_order; i++) {
    if (resH) { if (DmFindResource((DmOpenRef)res_order[i], 0, 0, resH) != 0xFFFF) { if (dbPP) *dbPP = (DmOpenRef)res_order[i]; return errNone; } }
    else if (res_find(res_order[i], resType, resID) >= 0) { if (dbPP) *dbPP = (DmOpenRef)res_order[i]; return errNone; }
  }
  return dmErrResourceNotFound;
}

static Err ram_add(ram_db_t *r, uint16_t at, MemHandle h) {
  if (r->nrecs == r->cap) {
    uint16_t cap = r->cap ? r->cap * 2 : 8;
    uint32_t **n = heap_realloc(r->recs, cap * sizeof(uint32_t *));
    if (!n) return dmErrMemError;
    r->recs = n; r->cap = cap;
  }
  if (at > r->nrecs) at = r->nrecs;
  memmove(&r->recs[at + 1], &r->recs[at], (r->nrecs - at) * sizeof(uint32_t *));
  r->recs[at] = (uint32_t *)h;
  r->nrecs++;
  r->dirty = 1;
  return errNone;
}

MemHandle DmNewResourceEx(DmOpenRef dbP, DmResType resType, DmResID resID, UInt32 size, void *p) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  chunk_t *c;
  if (!o || !o->is_ram || !o->is_res) return NULL;
  if ((h = MemHandleNew(size)) == NULL) return NULL;
  c = hchunk(h);
  c->type = resType; c->id = resID; c->flags |= CF_RES;
  if (p) memcpy((void *)*(uint32_t *)h, p, size);
  if (ram_add(o->ram, o->ram->nrecs, h)) { MemHandleFree(h); return NULL; }
  return h;
}

MemHandle DmNewResource(DmOpenRef dbP, DmResType resType, DmResID resID, UInt32 size) { return DmNewResourceEx(dbP, resType, resID, size, NULL); }

Err DmRemoveResource(DmOpenRef dbP, UInt16 index) {
  open_db_t *o = ref(dbP);
  if (!o || !o->is_ram || index >= o->ram->nrecs) return dmErrInvalidParam;
  MemHandleFree((MemHandle)o->ram->recs[index]);
  memmove(&o->ram->recs[index], &o->ram->recs[index + 1], (o->ram->nrecs - index - 1) * sizeof(uint32_t *));
  o->ram->nrecs--;
  o->ram->dirty = 1;
  return errNone;
}

MemHandle DmResizeResource(MemHandle resourceH, UInt32 newSize) { return MemHandleResize(resourceH, newSize) == errNone ? resourceH : NULL; }

Err DmSetResourceInfo(DmOpenRef dbP, UInt16 index, DmResType *resTypeP, DmResID *resIDP) {
  open_db_t *o = ref(dbP);
  chunk_t *c;
  if (!o || !o->is_ram || index >= o->ram->nrecs) return dmErrInvalidParam;
  c = hchunk((MemHandle)o->ram->recs[index]);
  if (resTypeP) c->type = *resTypeP;
  if (resIDP) c->id = *resIDP;
  o->ram->dirty = 1;
  return errNone;
}

void *StoNewDecodedResource(void *h, UInt32 size, DmResType resType, DmResID resID) {
  void *p = heap_alloc(size);
  (void)h; (void)resType; (void)resID;
  return p;
}

/* ---- records ---- */

static void rec_info(MemHandle h, UInt16 *attr, UInt32 *uid) {
  chunk_t *c = hchunk(h);
  if (IS_ROM(c)) {
    if (attr) *attr = c->attr >> 8;
    if (uid) *uid = ((uint32_t)(c->attr & 0xFF) << 16) | c->id;
  } else {
    if (attr) *attr = c->attr;
    if (uid) *uid = c->type;
  }
}

UInt16 DmNumRecords(DmOpenRef dbP) { open_db_t *o = ref(dbP); return o ? (UInt16)db_count(o) : 0; }

UInt16 DmNumRecordsInCategory(DmOpenRef dbP, UInt16 category) {
  open_db_t *o = ref(dbP);
  uint32_t i, n, k = 0;
  UInt16 attr;
  if (!o) return 0;
  n = db_count(o);
  for (i = 0; i < n; i++) {
    rec_info(db_handle(o, (uint16_t)i), &attr, NULL);
    if (attr & dmRecAttrDelete) continue;
    if (category == dmAllCategories || (attr & dmRecAttrCategoryMask) == category) k++;
  }
  return (UInt16)k;
}

MemHandle DmQueryRecord(DmOpenRef dbP, UInt16 index) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || o->is_res) return NULL;
  if ((h = db_handle(o, index)) == NULL) { last_err = dmErrIndexOutOfRange; return NULL; }
  return h;
}

MemHandle DmGetRecord(DmOpenRef dbP, UInt16 index) {
  MemHandle h = DmQueryRecord(dbP, index);
  if (h && !IS_ROM(h)) hchunk(h)->attr |= dmRecAttrBusy;
  return h;
}

Err DmReleaseRecord(DmOpenRef dbP, UInt16 index, Boolean dirty) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || (h = db_handle(o, index)) == NULL) return dmErrIndexOutOfRange;
  if (!IS_ROM(h)) {
    hchunk(h)->attr &= ~dmRecAttrBusy;
    if (dirty) { hchunk(h)->attr |= dmRecAttrDirty; o->ram->dirty = 1; }
  }
  return errNone;
}

MemHandle DmNewRecordEx(DmOpenRef dbP, UInt16 *atP, UInt32 size, void *p, UInt32 uniqueID, UInt16 attr, Boolean setAttr) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  chunk_t *c;
  uint16_t at;
  if (!o || !o->is_ram || o->is_res) { last_err = dmErrReadOnly; return NULL; }
  if ((h = MemHandleNew(size)) == NULL) { last_err = dmErrMemError; return NULL; }
  c = hchunk(h);
  c->flags |= CF_REC;
  c->type = uniqueID ? uniqueID : o->ram->seed++;
  c->attr = setAttr ? attr : dmRecAttrBusy;
  if (p) memcpy((void *)*(uint32_t *)h, p, size);
  at = atP ? *atP : dmMaxRecordIndex;
  if (at > o->ram->nrecs) at = o->ram->nrecs;
  if (ram_add(o->ram, at, h)) { MemHandleFree(h); return NULL; }
  if (atP) *atP = at;
  return h;
}

MemHandle DmNewRecord(DmOpenRef dbP, UInt16 *atP, UInt32 size) { return DmNewRecordEx(dbP, atP, size, NULL, 0, 0, false); }

MemHandle DmNewHandle(DmOpenRef dbP, UInt32 size) { (void)dbP; return MemHandleNew(size); }

Err DmAttachRecord(DmOpenRef dbP, UInt16 *atP, MemHandle newH, MemHandle *oldHP) {
  open_db_t *o = ref(dbP);
  uint16_t at;
  if (!o || !o->is_ram || o->is_res || !newH) return dmErrInvalidParam;
  hchunk(newH)->flags |= CF_REC;
  hchunk(newH)->type = o->ram->seed++;
  at = atP ? *atP : dmMaxRecordIndex;
  if (at > o->ram->nrecs) at = o->ram->nrecs;
  if (oldHP && at < o->ram->nrecs) { *oldHP = (MemHandle)o->ram->recs[at]; o->ram->recs[at] = (uint32_t *)newH; return errNone; }
  if (atP) *atP = at;
  return ram_add(o->ram, at, newH);
}

Err DmDetachRecord(DmOpenRef dbP, UInt16 index, MemHandle *oldHP) {
  open_db_t *o = ref(dbP);
  if (!o || !o->is_ram || index >= o->ram->nrecs) return dmErrInvalidParam;
  if (oldHP) *oldHP = (MemHandle)o->ram->recs[index];
  memmove(&o->ram->recs[index], &o->ram->recs[index + 1], (o->ram->nrecs - index - 1) * sizeof(uint32_t *));
  o->ram->nrecs--;
  o->ram->dirty = 1;
  return errNone;
}

Err DmRemoveRecord(DmOpenRef dbP, UInt16 index) {
  MemHandle h;
  Err err;
  open_db_t *o = ref(dbP);
  if (!o || !o->is_ram) return dmErrReadOnly;
  if ((err = DmDetachRecord(dbP, index, &h)) == errNone) MemHandleFree(h);
  return err;
}

Err DmDeleteRecord(DmOpenRef dbP, UInt16 index) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || !o->is_ram || (h = db_handle(o, index)) == NULL) return dmErrInvalidParam;
  hchunk(h)->attr |= dmRecAttrDelete;
  o->ram->dirty = 1;
  return errNone;
}

Err DmArchiveRecord(DmOpenRef dbP, UInt16 index) { return DmDeleteRecord(dbP, index); }

MemHandle DmResizeRecord(DmOpenRef dbP, UInt16 index, UInt32 newSize) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || !o->is_ram || (h = db_handle(o, index)) == NULL) return NULL;
  if (MemHandleResize(h, newSize) != errNone) return NULL;
  o->ram->dirty = 1;
  return h;
}

Err DmRecordInfo(DmOpenRef dbP, UInt16 index, UInt16 *attrP, UInt32 *uniqueIDP, LocalID *chunkIDP) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  if (!o || (h = db_handle(o, index)) == NULL) return dmErrIndexOutOfRange;
  rec_info(h, attrP, uniqueIDP);
  if (chunkIDP) *chunkIDP = (LocalID)h;
  return errNone;
}

Err DmSetRecordInfo(DmOpenRef dbP, UInt16 index, UInt16 *attrP, UInt32 *uniqueIDP) {
  open_db_t *o = ref(dbP);
  MemHandle h;
  chunk_t *c;
  if (!o || !o->is_ram || (h = db_handle(o, index)) == NULL) return dmErrInvalidParam;
  c = hchunk(h);
  if (attrP) c->attr = (c->attr & dmRecAttrBusy) | (*attrP & ~dmRecAttrBusy);
  if (uniqueIDP) c->type = *uniqueIDP;
  o->ram->dirty = 1;
  return errNone;
}

Err DmFindRecordByID(DmOpenRef dbP, UInt32 uniqueID, UInt16 *indexP) {
  open_db_t *o = ref(dbP);
  uint32_t i, n;
  UInt32 uid;
  if (!o) return dmErrInvalidParam;
  n = db_count(o);
  for (i = 0; i < n; i++) {
    rec_info(db_handle(o, (uint16_t)i), NULL, &uid);
    if (uid == uniqueID) { if (indexP) *indexP = (UInt16)i; return errNone; }
  }
  return dmErrCantFind;
}

Err DmMoveRecord(DmOpenRef dbP, UInt16 from, UInt16 to) {
  open_db_t *o = ref(dbP);
  uint32_t *h;
  if (!o || !o->is_ram || from >= o->ram->nrecs) return dmErrInvalidParam;
  h = o->ram->recs[from];
  memmove(&o->ram->recs[from], &o->ram->recs[from + 1], (o->ram->nrecs - from - 1) * sizeof(uint32_t *));
  if (to > from) to--;
  if (to > o->ram->nrecs - 1) to = o->ram->nrecs - 1;
  memmove(&o->ram->recs[to + 1], &o->ram->recs[to], (o->ram->nrecs - 1 - to) * sizeof(uint32_t *));
  o->ram->recs[to] = h;
  o->ram->dirty = 1;
  return errNone;
}

MemHandle DmQueryNextInCategory(DmOpenRef dbP, UInt16 *indexP, UInt16 category) {
  open_db_t *o = ref(dbP);
  uint32_t i, n;
  UInt16 attr;
  if (!o) return NULL;
  n = db_count(o);
  for (i = *indexP + 1; i < n; i++) {
    MemHandle h = db_handle(o, (uint16_t)i);
    rec_info(h, &attr, NULL);
    if ((attr & dmRecAttrDelete) == 0 && (category == dmAllCategories || (attr & dmRecAttrCategoryMask) == category)) { *indexP = (UInt16)i; return h; }
  }
  return NULL;
}

Err DmSeekRecordInCategory(DmOpenRef dbP, UInt16 *indexP, UInt16 offset, Int16 direction, UInt16 category) {
  open_db_t *o = ref(dbP);
  int32_t i, n;
  UInt16 attr;
  if (!o) return dmErrInvalidParam;
  n = db_count(o);
  i = *indexP;
  for (;;) {
    if (i < 0 || i >= n) return dmErrSeekFailed;
    rec_info(db_handle(o, (uint16_t)i), &attr, NULL);
    if ((attr & dmRecAttrDelete) == 0 && (category == dmAllCategories || (attr & dmRecAttrCategoryMask) == category)) {
      if (offset == 0) { *indexP = (UInt16)i; return errNone; }
      offset--;
    }
    i += direction < 0 ? -1 : 1;
  }
}

UInt16 DmPositionInCategory(DmOpenRef dbP, UInt16 index, UInt16 category) {
  open_db_t *o = ref(dbP);
  uint32_t i, k = 0;
  UInt16 attr;
  if (!o) return 0;
  for (i = 0; i < index && i < db_count(o); i++) {
    rec_info(db_handle(o, (uint16_t)i), &attr, NULL);
    if ((attr & dmRecAttrDelete) == 0 && (category == dmAllCategories || (attr & dmRecAttrCategoryMask) == category)) k++;
  }
  return (UInt16)k;
}

Err DmMoveCategory(DmOpenRef dbP, UInt16 toCategory, UInt16 fromCategory, Boolean dirty) { (void)dbP; (void)toCategory; (void)fromCategory; (void)dirty; return errNone; }
Err DmDeleteCategory(DmOpenRef dbR, UInt16 categoryNum) { (void)dbR; (void)categoryNum; return errNone; }
Err DmResetRecordStates(DmOpenRef dbP) { (void)dbP; return errNone; }
Err DmRemoveSecretRecords(DmOpenRef dbP) { (void)dbP; return errNone; }

Err DmWrite(void *recordP, UInt32 offset, const void *srcP, UInt32 bytes) {
  if (!recordP || !srcP) return dmErrInvalidParam;
  if (IS_ROM(recordP)) { debug(DEBUG_INFO, "STOR", "DmWrite into ROM ignored"); return dmErrReadOnly; }
  memmove((uint8_t *)recordP + offset, srcP, bytes);
  return errNone;
}

Err DmWriteCheck(void *recordP, UInt32 offset, UInt32 bytes) { (void)recordP; (void)offset; (void)bytes; return errNone; }

Err DmSet(void *recordP, UInt32 offset, UInt32 bytes, UInt8 value) {
  if (!recordP || IS_ROM(recordP)) return dmErrInvalidParam;
  memset((uint8_t *)recordP + offset, value, bytes);
  return errNone;
}

Err DmStrCopy(void *recordP, UInt32 offset, const Char *srcP) {
  if (!recordP || !srcP || IS_ROM(recordP)) return dmErrInvalidParam;
  strcpy((char *)recordP + offset, srcP);
  return errNone;
}

/* sorting: PumpkinOS's 68k-aware versions, on the record array */
static Int16 dm_compare(open_db_t *o, DmComparF *comparF, Int16 other, MemHandle a, MemHandle b) {
  UInt16 aa, ab;
  UInt32 ua, ub;
  SortRecordInfoType ia, ib;
  rec_info(a, &aa, &ua); rec_info(b, &ab, &ub);
  ia.attributes = (UInt8)aa; ia.uniqueID[0] = ua >> 16; ia.uniqueID[1] = ua >> 8; ia.uniqueID[2] = ua;
  ib.attributes = (UInt8)ab; ib.uniqueID[0] = ub >> 16; ib.uniqueID[1] = ub >> 8; ib.uniqueID[2] = ub;
  (void)o;
  return comparF((void *)*(uint32_t *)a, (void *)*(uint32_t *)b, other, &ia, &ib, NULL);
}

Err DmInsertionSort(DmOpenRef dbP, DmComparF *comparF, Int16 other) {
  open_db_t *o = ref(dbP);
  int i, j;
  uint32_t *h;
  if (!o || !o->is_ram) return dmErrReadOnly;
  for (i = 1; i < o->ram->nrecs; i++) {
    h = o->ram->recs[i];
    for (j = i; j > 0 && dm_compare(o, comparF, other, (MemHandle)o->ram->recs[j - 1], (MemHandle)h) > 0; j--) o->ram->recs[j] = o->ram->recs[j - 1];
    o->ram->recs[j] = h;
  }
  o->ram->dirty = 1;
  return errNone;
}

Err DmQuickSort(DmOpenRef dbP, DmComparF *comparF, Int16 other) { return DmInsertionSort(dbP, comparF, other); }

UInt16 DmFindSortPosition(DmOpenRef dbP, void *newRecord, SortRecordInfoPtr newRecordInfo, DmComparF *comparF, Int16 other) {
  open_db_t *o = ref(dbP);
  uint32_t i, n;
  if (!o) return 0;
  n = db_count(o);
  for (i = 0; i < n; i++) {
    MemHandle h = db_handle(o, (uint16_t)i);
    UInt16 attr; UInt32 uid; SortRecordInfoType info;
    rec_info(h, &attr, &uid);
    info.attributes = (UInt8)attr; info.uniqueID[0] = uid >> 16; info.uniqueID[1] = uid >> 8; info.uniqueID[2] = uid;
    if (comparF(newRecord, (void *)*(uint32_t *)h, other, newRecordInfo, &info, NULL) < 0) return (UInt16)i;
  }
  return (UInt16)n;
}

UInt16 DmFindSortPositionV10(DmOpenRef dbP, void *newRecord, DmComparF *comparF, Int16 other) { return DmFindSortPosition(dbP, newRecord, NULL, comparF, other); }

/* ---- the application database ---- */

DmOpenRef storage_open_app(const char *name) {
  LocalID id = DmFindDatabase(0, name);
  DmOpenRef db;
  if (!id) { debug(DEBUG_ERROR, "STOR", "application \"%s\" not found", name); return NULL; }
  db = DmOpenDatabase(0, id, dmModeReadWrite);
  app_db = db;
  return db;
}

/* ---- persistence: RAM databases and preferences go to the cartridge SRAM ---- */

#define SRAM_SIZE 0x8000
#define SRAM_MAGIC 0x53444F42u   /* 'BODS' */

typedef struct pref { uint32_t creator; uint16_t id; uint16_t size; uint8_t saved; uint8_t used; uint8_t *data; } pref_t;
#define MAX_PREFS 24
static pref_t prefs[MAX_PREFS];
static int sram_present = -1;

static void sram_write(uint32_t off, const void *p, uint32_t n) { const uint8_t *s = p; while (n--) SRAM[off++] = *s++; }
static void sram_read(uint32_t off, void *p, uint32_t n) { uint8_t *d = p; while (n--) *d++ = SRAM[off++]; }
static uint32_t sram_u32(uint32_t off) { uint32_t v; sram_read(off, &v, 4); return v; }

static int sram_check(void) {
  if (sram_present < 0) {
    uint8_t a = SRAM[SRAM_SIZE - 1], b;
    SRAM[SRAM_SIZE - 1] = (uint8_t)~a;
    b = SRAM[SRAM_SIZE - 1];
    SRAM[SRAM_SIZE - 1] = a;
    sram_present = (b == (uint8_t)~a);
    if (!sram_present) debug(DEBUG_ERROR, "STOR", "no cartridge SRAM: nothing will be saved");
  }
  return sram_present;
}

void storage_sync(void) {
  uint32_t off = 8, i, j, n = 0;
  if (!sram_check()) return;
  for (i = 0; i < MAX_PREFS; i++) if (prefs[i].used) {
    uint8_t hdr[12];
    if (off + 12 + prefs[i].size > SRAM_SIZE - 8) break;
    memcpy(hdr, &prefs[i].creator, 4); memcpy(hdr + 4, &prefs[i].id, 2); memcpy(hdr + 6, &prefs[i].size, 2); hdr[8] = 'P'; hdr[9] = prefs[i].saved; hdr[10] = hdr[11] = 0;
    sram_write(off, hdr, 12); off += 12;
    sram_write(off, prefs[i].data, prefs[i].size); off += prefs[i].size;
    n++;
  }
  for (i = 0; i < MAX_RAMDB; i++) {
    ram_db_t *r = &ramdbs[i];
    uint32_t need = 64 + 4, k;
    if (!r->used) continue;
    for (j = 0; j < r->nrecs; j++) need += 12 + heap_size((void *)*r->recs[j]);
    if (off + need > SRAM_SIZE - 8) { debug(DEBUG_ERROR, "STOR", "SRAM full, \"%s\" not saved", r->name); break; }
    {
      uint8_t hdr[64];
      memset(hdr, 0, sizeof hdr);
      memcpy(hdr, r->name, 32); memcpy(hdr + 32, &r->type, 4); memcpy(hdr + 36, &r->creator, 4);
      memcpy(hdr + 40, &r->attrs, 2); memcpy(hdr + 42, &r->version, 2); memcpy(hdr + 44, &r->crDate, 4); memcpy(hdr + 48, &r->modDate, 4);
      memcpy(hdr + 52, &r->seed, 4); memcpy(hdr + 56, &r->nrecs, 2); hdr[58] = r->is_res; hdr[59] = 'D';
      sram_write(off, hdr, 64); off += 64;
    }
    for (j = 0; j < r->nrecs; j++) {
      chunk_t *c = hchunk((MemHandle)r->recs[j]);
      uint8_t rh[12];
      k = c->size;
      memcpy(rh, &c->type, 4); memcpy(rh + 4, &c->id, 2); memcpy(rh + 6, &c->attr, 2); memcpy(rh + 8, &k, 4);
      sram_write(off, rh, 12); off += 12;
      sram_write(off, (void *)*r->recs[j], k); off += k;
    }
    r->dirty = 0;
    n++;
  }
  {
    uint32_t magic = SRAM_MAGIC, end = 0xFFFFFFFFu;
    sram_write(off, &end, 4);
    sram_write(0, &magic, 4);
    sram_write(4, &n, 4);
  }
  debug(DEBUG_INFO, "STOR", "saved %u objects, %u bytes of SRAM", n, off);
}

void storage_load(void) {
  uint32_t off = 8, n, i, j;
  if (!sram_check()) return;
  if (sram_u32(0) != SRAM_MAGIC) {
    /* first boot: the game's settings as saved by a Palm (files/bikd-prefs-3.bin) */
    LocalID id; DmOpenRef db; UInt16 index; MemHandle h;
    debug(DEBUG_INFO, "STOR", "SRAM is blank");
    if ((id = DmFindDatabase(0, "Precomputed")) != 0 && (db = DmOpenDatabase(0, id, dmModeReadOnly)) != NULL &&
        (index = DmFindResource(db, 'pref', 3, NULL)) != 0xFFFF && (h = DmGetResourceIndex(db, index)) != NULL) {
      void *p = MemHandleLock(h);
      pumpkin_set_preference('BiKD', 3, p, MemHandleSize(h), true);
      debug(DEBUG_INFO, "STOR", "seeded %u bytes of game settings", (unsigned)MemHandleSize(h));
    }
    return;
  }
  n = sram_u32(4);
  for (i = 0; i < n && off < SRAM_SIZE - 12; i++) {
    uint8_t tag = SRAM[off + 8];
    if (tag == 'P') {
      uint8_t hdr[12];
      sram_read(off, hdr, 12); off += 12;
      {
        uint32_t creator; uint16_t id, size;
        memcpy(&creator, hdr, 4); memcpy(&id, hdr + 4, 2); memcpy(&size, hdr + 6, 2);
        for (j = 0; j < MAX_PREFS; j++) if (!prefs[j].used) break;
        if (j < MAX_PREFS) {
          prefs[j].used = 1; prefs[j].creator = creator; prefs[j].id = id; prefs[j].size = size; prefs[j].saved = hdr[9];
          prefs[j].data = heap_alloc(size);
          sram_read(off, prefs[j].data, size);
        }
        off += size;
      }
    } else if (SRAM[off + 59] == 'D') {
      uint8_t hdr[64];
      ram_db_t *r;
      uint16_t nrecs;
      sram_read(off, hdr, 64); off += 64;
      for (j = 0; j < MAX_RAMDB; j++) if (!ramdbs[j].used) break;
      if (j == MAX_RAMDB) break;
      r = &ramdbs[j];
      memset(r, 0, sizeof *r);
      r->used = 1;
      memcpy(r->name, hdr, 32); memcpy(&r->type, hdr + 32, 4); memcpy(&r->creator, hdr + 36, 4);
      memcpy(&r->attrs, hdr + 40, 2); memcpy(&r->version, hdr + 42, 2); memcpy(&r->crDate, hdr + 44, 4); memcpy(&r->modDate, hdr + 48, 4);
      memcpy(&r->seed, hdr + 52, 4); memcpy(&nrecs, hdr + 56, 2); r->is_res = hdr[58];
      for (j = 0; j < nrecs; j++) {
        uint8_t rh[12];
        uint32_t uid, size; uint16_t id, attr;
        MemHandle h;
        sram_read(off, rh, 12); off += 12;
        memcpy(&uid, rh, 4); memcpy(&id, rh + 4, 2); memcpy(&attr, rh + 6, 2); memcpy(&size, rh + 8, 4);
        if ((h = MemHandleNew(size)) == NULL) break;
        hchunk(h)->type = uid; hchunk(h)->id = id; hchunk(h)->attr = attr & ~dmRecAttrBusy;
        hchunk(h)->flags |= r->is_res ? CF_RES : CF_REC;
        sram_read(off, (void *)*(uint32_t *)h, size); off += size;
        ram_add(r, r->nrecs, h);
      }
      r->dirty = 0;
      debug(DEBUG_INFO, "STOR", "restored \"%s\" (%d records)", r->name, r->nrecs);
    } else break;
  }
}

void pumpkin_set_preference(UInt32 creator, UInt16 id, void *p, UInt16 size, Boolean saved) {
  int i, free = -1;
  for (i = 0; i < MAX_PREFS; i++) {
    if (prefs[i].used && prefs[i].creator == creator && prefs[i].id == id && prefs[i].saved == saved) break;
    if (!prefs[i].used && free < 0) free = i;
  }
  if (i == MAX_PREFS) { if (free < 0) return; i = free; prefs[i].used = 1; prefs[i].creator = creator; prefs[i].id = id; prefs[i].saved = saved; prefs[i].data = NULL; }
  if (prefs[i].data) heap_free(prefs[i].data);
  prefs[i].data = heap_alloc(size);
  if (prefs[i].data && p) memcpy(prefs[i].data, p, size);
  prefs[i].size = size;
  storage_sync();
}

UInt16 pumpkin_get_preference(UInt32 creator, UInt16 id, void *p, UInt16 size, Boolean saved) {
  int i;
  for (i = 0; i < MAX_PREFS; i++) {
    if (prefs[i].used && prefs[i].creator == creator && prefs[i].id == id && prefs[i].saved == saved) {
      if (p == NULL || size == 0) return prefs[i].size;
      if (size > prefs[i].size) size = prefs[i].size;
      memcpy(p, prefs[i].data, size);
      return size;
    }
  }
  return 0;
}

void pumpkin_delete_preferences(UInt32 creator, Boolean saved) {
  int i;
  for (i = 0; i < MAX_PREFS; i++) if (prefs[i].used && prefs[i].creator == creator && prefs[i].saved == saved) { heap_free(prefs[i].data); prefs[i].used = 0; }
}
