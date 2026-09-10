/* The pumpkin_* services libpumpkin expects from its host, on the GBA: one
 * task, one screen, the console's buttons for a keyboard and a navigator. */
#include <PalmOS.h>
#include <VFSMgr.h>
#include "sys.h"
#include "pwindow.h"
#include "pumpkin.h"
#include "mutex.h"
#include "storage.h"
#include "bod.h"
#include "debug.h"
#include "emupalmos.h"
#include "heap.h"
#include "libc.h"
#include "gba.h"
#include "r68k.h"

#define FONT_DOUBLE_BASE 9000
#define FONT_LOW_BASE    9100

static void *local_storage[last_key];
static FontType *fontPtrV1[128];
static FontType *fontPtrV2[128];
static int must_finish;
static Err last_err;
static int native_keys;
static int osversion = 54;

int pumpkin_set_local_storage(local_storage_key_t key, void *p) { if (key < last_key) { local_storage[key] = p; return 0; } return -1; }
void *pumpkin_get_local_storage(local_storage_key_t key) { return key < last_key ? local_storage[key] : NULL; }

void *pumpkin_heap_base(void) { return NULL; }
uint32_t pumpkin_heap_size(void) { return 0x10000000; }
void *pumpkin_heap_alloc(uint32_t size, char *tag) { return heap_alloc_tag(size, tag); }
void *pumpkin_heap_realloc(void *p, uint32_t size, char *tag) { (void)tag; return heap_realloc(p, size); }
void pumpkin_heap_free(void *p, char *tag) { (void)tag; heap_free(p); }
void *pumpkin_heap_dup(void *p, uint32_t size, char *tag) { void *q = pumpkin_heap_alloc(size, tag); if (q && p) memcpy(q, p, size); return q; }
void pumpkin_heap_dump(void) { heap_dump(); }

void pumpkin_set_osversion(int version) { osversion = version; }
int pumpkin_get_osversion(void) { return osversion; }
int pumpkin_get_default_osversion(void) { return 54; }
int pumpkin_get_density(void) { return kDensityLow; }
int pumpkin_get_depth(void) { return 8; }
int pumpkin_get_mode(void) { return 1; }
void pumpkin_get_window(int *width, int *height) { if (width) *width = 240; if (height) *height = 160; }
int pumpkin_get_encoding(void) { return charEncodingPalmLatin; }
int pumpkin_is_m68k(void) { return 1; }
void pumpkin_set_m68k(int m68k) { (void)m68k; }
int pumpkin_get_current(void) { return 0; }
uint32_t pumpkin_get_taskid(void) { return 1; }
UInt32 pumpkin_get_app_creator(void) { return 'BiKD'; }
LocalID pumpkin_get_app_localid(void) { LocalID id = 0; DmOpenDatabaseInfo(storage_app_db(), &id, NULL, NULL, NULL, NULL); return id; }
uint32_t pumpkin_get_param_size(void) { return 0; }
int pumpkin_is_launched(void) { return 1; }
int pumpkin_is_paused(void) { return 0; }
int pumpkin_pause(int pause) { (void)pause; return 0; }
void pumpkin_set_finish(int finish) { must_finish = finish; }
int pumpkin_must_finish(void) { return must_finish || emupalmos_finished(); }
void pumpkin_set_lasterr(Err err) { last_err = err; }
Err pumpkin_get_lasterr(void) { return last_err; }
const char *pumpkin_error_msg(Err err) { (void)err; return "error"; }
int pumpkin_get_battery(void) { return 100; }
void pumpkin_set_battery(int level) { (void)level; }
int pumpkin_dia_enabled(void) { return 0; }
int pumpkin_dia_get_state(void) { return 0; }
int pumpkin_dia_set_state(int state) { (void)state; return -1; }
int pumpkin_dia_get_trigger(void) { return 0; }
int pumpkin_dia_set_trigger(int trigger) { (void)trigger; return -1; }
int pumpkin_dia_set_graffiti_state(int state) { (void)state; return -1; }
int pumpkin_dia_get_taskbar_dimension(int *width, int *height) { if (width) *width = 0; if (height) *height = 0; return -1; }
int pumpkin_get_boolean_option(char *name) { (void)name; return 0; }
int pumpkin_get_integer_option(char *name) { (void)name; return 0; }
uint32_t pumpkin_get_id_option(char *name) { (void)name; return 0; }
char *pumpkin_get_string_option(char *name) { (void)name; return NULL; }
void *pumpkin_reg_get(DmResType type, UInt16 id, UInt32 *size) { (void)type; (void)id; if (size) *size = 0; return NULL; }
Err pumpkin_reg_set(DmResType type, UInt16 id, void *p, UInt32 size) { (void)type; (void)id; (void)p; (void)size; return errNone; }
void pumpkin_trace(uint16_t trap) { (void)trap; }
void pumpkin_save_bitmap(BitmapType *bmp, UInt16 density, Coord wWidth, Coord wHeight, Coord width, Coord height, char *filename) { (void)bmp; (void)density; (void)wWidth; (void)wHeight; (void)width; (void)height; (void)filename; }
void pumpkin_save_bmp(char *dbname, UInt32 type, UInt16 id, char *filename) { (void)dbname; (void)type; (void)id; (void)filename; }
void pumpkin_error_dialog(char *msg) { debug(DEBUG_ERROR, "PUMPKIN", "error dialog: %s", msg); }
void pumpkin_fatal_error(int finish) { debug(DEBUG_ERROR, "PUMPKIN", "fatal error"); if (finish) must_finish = 1; }
void pumpkin_generic_error(char *msg, int code) { debug(DEBUG_ERROR, "PUMPKIN", "error %d: %s", code, msg); }
void pumpkin_crash_log(UInt32 creator, int code, char *msg) { (void)creator; debug(DEBUG_ERROR, "PUMPKIN", "crash %d: %s", code, msg); }
void pumpkin_app_crashed(void) { must_finish = 1; }
void pumpkin_set_native_keys(int active) { native_keys = active; }
int pumpkin_get_native_keys(void) { return native_keys; }
void pumpkin_keymask(uint32_t keyMask) { (void)keyMask; }
int32_t pumpkin_event_timeout(int32_t t) { (void)t; return 0; }
void pumpkin_dirty_region_mode(dirty_region_e d) { (void)d; }
void pumpkin_set_data(void *data) { local_storage[data_key] = data; }
void *pumpkin_get_data(void) { return local_storage[data_key]; }
int pumpkin_alarm_check(void) { return 0; }
int pumpkin_alarm_set(LocalID dbID, uint32_t t, uint32_t data) { (void)dbID; (void)t; (void)data; return 0; }
int pumpkin_alarm_get(LocalID dbID, uint32_t *t, uint32_t *data) { (void)dbID; if (t) *t = 0; if (data) *data = 0; return -1; }
int pumpkin_clipboard_add_text(char *text, int length) { (void)text; (void)length; return 0; }
int pumpkin_clipboard_append_text(char *text, int length) { (void)text; (void)length; return 0; }
int pumpkin_clipboard_get_text(char *text, int *length) { (void)text; if (length) *length = 0; return 0; }
int pumpkin_clipboard_add_bitmap(BitmapType *bmp, int size) { (void)bmp; (void)size; return 0; }
void pumpkin_forward_event(int i, EventType *event) { (void)i; (void)event; }
void pumpkin_forward_msg(int i, int ev, int a1, int a2, int a3) { (void)i; (void)ev; (void)a1; (void)a2; (void)a3; }
void pumpkin_calibrate(int restore) { (void)restore; }
int pumpkin_change_display(int width, int height) { (void)width; (void)height; return 0; }
void pumpkin_taskbar_update(void) {}
void pumpkin_set_secure(void *secure) { (void)secure; }
uint32_t pumpkin_dt(void) { return 0; }
void pumpkin_local_refresh(void) {}
void pumpkin_refresh_desktop(void) {}
int pumpkin_sys_event(void) { return 0; }
int pumpkin_event_peek(void) { return 0; }

/* ---- character sets: single-byte Latin ---- */
UInt32 pumpkin_next_char(UInt8 *s, UInt32 i, UInt32 len, UInt32 *w) { (void)len; *w = s[i]; return 1; }
UInt8 pumpkin_map_char(UInt32 w, FontType **f) { *f = FntGetFontPtr(); return (UInt8)w; }
int pumpkin_getstr(char **s, uint8_t *p, int i) { *s = (char *)&p[i]; return sys_strlen(*s) + 1; }
char *pumpkin_id2s(UInt32 ID, char *s) { s[0] = ID >> 24; s[1] = ID >> 16; s[2] = ID >> 8; s[3] = ID; s[4] = 0; return s; }
UInt32 pumpkin_s2id(UInt32 *ID, char *s) { *ID = ((UInt32)(UInt8)s[0] << 24) | ((UInt32)(UInt8)s[1] << 16) | ((UInt32)(UInt8)s[2] << 8) | (UInt8)s[3]; return *ID; }

/* ---- fonts: the system fonts, copied out of the BOOT database once ---- */
static const int systemFonts[] = {
  stdFont, boldFont, largeFont, symbolFont, symbol11Font, symbol7Font, ledFont, largeBoldFont,
  mono6x10Font, mono8x14Font, mono16x16Font, mono8x16Font, tos8x16Font, tos8x8Font, -1 };

FontType *pumpkin_get_font(FontID fontId, UInt16 density) {
  if (fontId < 0 || fontId >= 128) return NULL;
  return density == kDensityLow ? fontPtrV1[fontId] : fontPtrV2[fontId];
}

void pumpkin_load_fonts(void) {
  int index;
  FontID fontId;
  MemHandle handle;
  FontPtr f;
  for (index = 0; systemFonts[index] >= 0; index++) {
    fontId = systemFonts[index];
    if ((handle = DmGetResource(fontRscType, FONT_LOW_BASE + fontId)) != NULL) {
      /* the decoded font is kept, resource and all: system fonts never go away */
      if ((f = MemHandleLock(handle)) != NULL) fontPtrV1[fontId] = f;
    } else {
      debug(DEBUG_ERROR, "PUMPKIN", "font %d not found", fontId);
    }
  }
}

/* ---- input: the console's buttons, as a keyboard and as a navigator ---- */
#define KEYQ 16
static struct { int ev, key; } keyq[KEYQ];
static volatile int keyq_in, keyq_out;
static u16 keys_last;
static volatile u32 palm_keymask;

static const struct { u16 gba; int win; u32 mask; } keymap[] = {
  { KEY_UP,     WINDOW_KEY_UP,    keyBitPageUp },
  { KEY_DOWN,   WINDOW_KEY_DOWN,  keyBitPageDown },
  { KEY_LEFT,   WINDOW_KEY_LEFT,  keyBitLeft | keyBitRockerLeft | keyBitNavLeft },
  { KEY_RIGHT,  WINDOW_KEY_RIGHT, keyBitRight | keyBitRockerRight | keyBitNavRight },
  { KEY_A,      WINDOW_KEY_F9,    keyBitNavSelect | keyBitRockerCenter },
  { KEY_B,      27,               keyBitHard1 },
  { KEY_START,  WINDOW_KEY_F5,    0 },
  { KEY_SELECT, WINDOW_KEY_F4,    keyBitHard4 },
  { KEY_L,      WINDOW_KEY_F2,    keyBitHard2 },
  { KEY_R,      WINDOW_KEY_F3,    keyBitHard3 },
};

static void keyq_push(int ev, int key) {
  int n = (keyq_in + 1) % KEYQ;
  if (n == keyq_out) return;
  keyq[keyq_in].ev = ev; keyq[keyq_in].key = key; keyq_in = n;
}

/* Called from the vblank interrupt (gba/crt0.s irq_hook): a press shorter
 * than one of the engine's frames would otherwise go unseen. */
void input_poll(void) {
  u16 now = (u16)KEYS_HELD(), diff = now ^ keys_last;
  unsigned i;
  if (!diff) return;
  if ((diff & now & KEY_SELECT) != 0) {
    /* Select switches between the sharp and the fast renderer, from the next level on */
    extern int bod_halfres;
    bod_halfres = !bod_halfres;
    debug(DEBUG_INFO, "GBA", "%s rendering from the next level", bod_halfres ? "half-size (fast)" : "full-size (sharp)");
  }
  for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
    if (!(diff & keymap[i].gba)) continue;
    if (now & keymap[i].gba) { keyq_push(MSG_KEYDOWN, keymap[i].win); palm_keymask |= keymap[i].mask; }
    else { keyq_push(MSG_KEYUP, keymap[i].win); palm_keymask &= ~keymap[i].mask; }
  }
  keys_last = now;
}

extern uint32_t bod_evt_calls;
int pumpkin_event(int *key, int *mods, int *buttons, uint8_t *data, uint32_t *n, uint32_t usec) {
  bod_evt_calls++;
  int ev;
  (void)data;
  *mods = 0; *buttons = 0; *n = 0; *key = 0;
  if (keyq_in == keyq_out) {
    /* A Palm would sleep here for the timeout; the engine itself takes longer
     * than a frame, so give the time straight back to it. Only an idle wait
     * (a long timeout while nothing is drawn) yields a frame. */
    if (usec >= 100000) vsync();
    if (keyq_in == keyq_out) return 0;
  }
  ev = keyq[keyq_out].ev; *key = keyq[keyq_out].key;
  keyq_out = (keyq_out + 1) % KEYQ;
  return ev;
}

void pumpkin_status(int *x, int *y, uint32_t *keyMask, uint32_t *modMask, uint32_t *buttonMask, uint64_t *extKeyMask) {
  if (x) *x = 0;
  if (y) *y = 0;
  if (keyMask) *keyMask = palm_keymask;
  if (modMask) *modMask = 0;
  if (buttonMask) *buttonMask = 0;
  if (extKeyMask) { extKeyMask[0] = 0; extKeyMask[1] = 0; }
}

int pumpkin_extkey_down(int key, uint64_t *extKeyMask) { (void)key; (void)extKeyMask; return 0; }

/* ---- the screen: the display window's bitmap goes to VRAM ---- */
void screen_copy_rect(const uint8_t *bits, int rowBytes, int x, int y, int w, int h);

void pumpkin_screen_dirty(WinHandle wh, int x, int y, int w, int h) {
  BitmapType *bmp;
  Coord sx, sy, bw, bh;
  UInt16 rowBytes;
  int bx = x, by = y;
  if (!wh || wh == WinGetDisplayWindow()) return;
  bmp = WinGetBitmap(wh);
  if (!bmp || BmpGetBitDepth(bmp) != 8) return;
  if (((uint32_t)BmpGetBits(bmp) >> 24) == 0x06) return;
  BmpGetDimensions(bmp, &bw, &bh, &rowBytes);
  WinGetPosition(wh, &sx, &sy);
  if (wh == WinGetDisplayWindow()) { sx = 0; sy = 0; }
  /* clip to the bitmap, then to the screen */
  if (bx < 0) { w += bx; x -= bx; bx = 0; }
  if (by < 0) { h += by; y -= by; by = 0; }
  if (bx + w > bw) w = bw - bx;
  if (by + h > bh) h = bh - by;
  x = bx + sx; y = by + sy;
  if (x < 0) { w += x; bx -= x; x = 0; }
  if (y < 0) { h += y; by -= y; y = 0; }
  if (x + w > 240) w = 240 - x;
  if (y + h > 160) h = 160 - y;
  if (w <= 0 || h <= 0) return;
  screen_copy_rect((const uint8_t *)BmpGetBits(bmp) + by * rowBytes + bx, rowBytes, x, y, w, h);
}

void pumpkin_screen_copy(uint16_t *src, uint16_t y0, uint16_t y1) { (void)src; (void)y0; (void)y1; }

/* ---- notifications: registered, never broadcast ---- */
Err SysNotifyRegister(UInt16 cardNo, LocalID dbID, UInt32 notifyType, SysNotifyProcPtr callbackP, Int8 priority, void *userDataP) {
  (void)cardNo; (void)dbID; (void)notifyType; (void)callbackP; (void)priority; (void)userDataP;
  return errNone;
}
Err SysNotifyUnregister(UInt16 cardNo, LocalID dbID, UInt32 notifyType, Int8 priority) { (void)cardNo; (void)dbID; (void)notifyType; (void)priority; return errNone; }
Err SysNotifyBroadcast(SysNotifyParamType *notify) { (void)notify; return errNone; }
Err SysNotifyBroadcastDeferred(SysNotifyParamType *notify, Int16 paramSize) { (void)notify; (void)paramSize; return errNone; }
void SysNotifyBroadcastQueued(void) {}

/* ---- calling back into the 68k code ---- */
Boolean CallFormHandler(UInt32 addr, EventType *eventP) {
  uint32_t buf[16];
  uint32_t a = (uint32_t)buf;
  Boolean handled;
  m68k_write_memory_32(a, a + 4);
  encode_event(a + 4, eventP);
  handled = (r68k_call(addr, buf, 4, 0) & 0xFF) != 0;
  return handled;
}

void CallListDrawItem(UInt32 addr, Int16 i, RectangleType *rect, char **text) {
  uint32_t buf[8];
  uint32_t a = (uint32_t)buf;
  m68k_write_memory_16(a, i);
  m68k_write_memory_32(a + 2, a + 12);
  m68k_write_memory_32(a + 6, text ? (uint32_t)&text[0] : 0);
  encode_rectangle(a + 12, rect);
  r68k_call(addr, buf, 10, 0);
}

Int16 CallCompareFunction(UInt32 comparF, void *e1, void *e2, Int32 other) {
  uint32_t buf[3];
  uint32_t a = (uint32_t)buf;
  m68k_write_memory_32(a, (uint32_t)e1);
  m68k_write_memory_32(a + 4, (uint32_t)e2);
  m68k_write_memory_32(a + 8, other);
  return (Int16)(r68k_call(comparF, buf, 12, 0) & 0xFFFF);
}

Boolean CallPrgCallback(UInt32 addr, UInt32 data) {
  uint32_t buf[1];
  m68k_write_memory_32((uint32_t)buf, data);
  return (r68k_call(addr, buf, 4, 0) & 0xFF) != 0;
}

Err CallNotifyProc(UInt32 addr, SysNotifyParamType *notify, UInt32 detailsSize) { (void)addr; (void)notify; (void)detailsSize; return errNone; }

Boolean CallGadgetHandler(UInt32 addr, FormGadgetTypeInCallback *gadgetP, UInt16 cmd, EventType *eventP) {
  uint32_t buf[32];
  uint32_t a = (uint32_t)buf;
  m68k_write_memory_32(a, a + 12);
  m68k_write_memory_16(a + 4, cmd);
  m68k_write_memory_32(a + 6, eventP ? a + 12 + 32 : 0);
  encode_gadget(a + 12, (FormGadgetType *)gadgetP);
  if (eventP) encode_event(a + 12 + 32, eventP);
  return (r68k_call(addr, buf, 10, 0) & 0xFF) != 0;
}

void CallTableDrawItem(UInt32 addr, TableType *tableP, Int16 row, Int16 column, RectangleType *rect) { (void)addr; (void)tableP; (void)row; (void)column; (void)rect; }
Boolean CallTableSaveData(UInt32 addr, TableType *tableP, Int16 row, Int16 column) { (void)addr; (void)tableP; (void)row; (void)column; return false; }
Err CallTableLoadData(UInt32 addr, TableType *tableP, Int16 row, Int16 column, Boolean editable, MemHandle *dataH, Int16 *dataOffset, Int16 *dataSize, FieldPtr fld) {
  (void)addr; (void)tableP; (void)row; (void)column; (void)editable; (void)dataH; (void)dataOffset; (void)dataSize; (void)fld; return errNone;
}
Int16 CallDmCompare(UInt32 addr, UInt32 rec1, UInt32 rec2, Int16 other, UInt32 rec1SortInfo, UInt32 rec2SortInfo, UInt32 appInfoH) {
  uint32_t buf[6];
  uint32_t a = (uint32_t)buf;
  m68k_write_memory_32(a, rec1); m68k_write_memory_32(a + 4, rec2); m68k_write_memory_16(a + 8, other);
  m68k_write_memory_32(a + 10, rec1SortInfo); m68k_write_memory_32(a + 14, rec2SortInfo); m68k_write_memory_32(a + 18, appInfoH);
  return (Int16)(r68k_call(addr, buf, 22, 0) & 0xFFFF);
}

/* ---- the list of databases a picker shows ---- */
Boolean SysCreateDataBaseList68K(UInt32 type, UInt32 creator, UInt16 *dbCount, MemHandle *dbIDs, Boolean lookupName) {
  DmSearchStateType st;
  UInt16 card, n = 0, i;
  LocalID id;
  MemHandle h;
  uint8_t *p;
  Boolean newSearch = true;
  (void)lookupName;
  while (DmGetNextDatabaseByTypeCreator(newSearch, &st, type, creator, false, &card, &id) == errNone) { newSearch = false; n++; }
  if (n == 0) { *dbCount = 0; *dbIDs = NULL; return false; }
  /* SysDBListItemType is 32+4+2 bytes, big-endian, since the 68k code walks it */
  if ((h = MemHandleNew(n * 38)) == NULL) return false;
  p = MemHandleLock(h);
  newSearch = true;
  for (i = 0; i < n && DmGetNextDatabaseByTypeCreator(newSearch, &st, type, creator, false, &card, &id) == errNone; i++) {
    uint8_t *e = p + i * 38;
    newSearch = false;
    DmDatabaseInfo(card, id, (char *)e, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    m68k_write_memory_32((uint32_t)(e + 32), id);
    m68k_write_memory_16((uint32_t)(e + 36), card);
  }
  MemHandleUnlock(h);
  *dbCount = n;
  *dbIDs = h;
  return true;
}
