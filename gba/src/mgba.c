/* mGBA's debug port: a 256-byte string buffer and a flags register the
 * emulator watches. Nothing is mapped there on real hardware. */
#include "gba.h"
#include "libc.h"

#define REG_DEBUG_ENABLE  (*(vu16 *)0x4FFF780)
#define REG_DEBUG_FLAGS   (*(vu16 *)0x4FFF700)
#define REG_DEBUG_STRING  ((volatile char *)0x4FFF600)

static int mgba_on;

void mgba_open(void) {
  REG_DEBUG_ENABLE = 0xC0DE;
  mgba_on = REG_DEBUG_ENABLE == 0x1DEA;
}

void mgba_log(int level, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  int i;
  if (!mgba_on) return;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  for (i = 0; i < 255 && buf[i]; i++) REG_DEBUG_STRING[i] = buf[i];
  REG_DEBUG_STRING[i] = 0;
  REG_DEBUG_FLAGS = (u16)(level & 7) | 0x100;
}
