/* Launching the 68k application: the same job PumpkinOS's emupalmos_main does
 * for a code 0 / code 1 / data 0 application, on the recompiled core. The A5
 * world is built from the compressed data resource and its relocation chains
 * (the format prc-tools and CodeWarrior both emit), then code 1 is entered
 * the way the OS would enter it. */
#include <PalmOS.h>
#include "sys.h"
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

#define STACK_SIZE 8192

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Decompress data 0 into the globals block and apply its relocations. */
static int load_globals(const uint8_t *data0, uint32_t data0Size, uint8_t *data, uint32_t dataSize, uint32_t dataStart, uint32_t codeStart) {
  uint32_t i, m, k, j, st, offset, count, value, xr;
  uint8_t b, n;
  uint8_t *start;
  int32_t code1_xrefs;

  code1_xrefs = (int32_t)be32(data0);
  i = 4;
  for (m = 0; m < 3; m++) {
    offset = be32(data0 + i); i += 4;
    st = dataSize + offset;
    start = &data[st];
    for (k = 0;;) {
      b = data0[i++];
      if (b == 0x00) break;
      if ((b & 0x80) == 0x80) { n = b & 0x7F; for (j = 0; j < (uint32_t)n + 1; j++) start[k++] = data0[i++]; }
      else if ((b & 0xC0) == 0x40) { n = b & 0x3F; for (j = 0; j < (uint32_t)n + 1; j++) start[k++] = 0x00; }
      else if ((b & 0xE0) == 0x20) { n = b & 0x1F; b = data0[i++]; for (j = 0; j < (uint32_t)n + 2; j++) start[k++] = b; }
      else if ((b & 0xF0) == 0x10) { n = b & 0x0F; for (j = 0; j < (uint32_t)n + 1; j++) start[k++] = 0xFF; }
      else if (b == 0x01) { start[k++] = 0; start[k++] = 0; start[k++] = 0; start[k++] = 0; start[k++] = 0xFF; start[k++] = 0xFF; start[k++] = data0[i++]; start[k++] = data0[i++]; }
      else if (b == 0x02) { start[k++] = 0; start[k++] = 0; start[k++] = 0; start[k++] = 0; start[k++] = 0xFF; start[k++] = data0[i++]; start[k++] = data0[i++]; start[k++] = data0[i++]; }
      else if (b == 0x03) { start[k++] = 0xA9; start[k++] = 0xF0; start[k++] = 0; start[k++] = 0; start[k++] = data0[i++]; start[k++] = data0[i++]; start[k++] = 0; start[k++] = data0[i++]; }
      else if (b == 0x04) { start[k++] = 0xA9; start[k++] = 0xF0; start[k++] = 0; start[k++] = data0[i++]; start[k++] = data0[i++]; start[k++] = data0[i++]; start[k++] = 0; start[k++] = data0[i++]; }
      else { debug(DEBUG_ERROR, "APP", "data 0: unknown block 0x%02X at %u", b, i - 1); return -1; }
    }
  }

  if (i < data0Size - 12) {
    for (m = 0; m < 3; m++) {
      uint32_t segment, relocbase;
      count = be32(data0 + i); i += 4;
      relocbase = (m == 0) ? dataStart + dataSize : codeStart;
      segment = dataStart + dataSize;
      offset = 0;
      for (xr = 0; xr < count; xr++) {
        b = data0[i++];
        if (b & 0x80) {
          int8_t d = (int8_t)b; d <<= 1; offset += d;
        } else if (b & 0x40) {
          int16_t w = b; w <<= 8; w |= data0[i++]; w <<= 2; w >>= 1; offset += w;
        } else {
          int32_t l = b; l <<= 8; l |= data0[i++]; l <<= 8; l |= data0[i++]; l <<= 8; l |= data0[i++]; l <<= 2; l >>= 1; offset = l;
        }
        value = rd32(segment + offset);
        if (m < 2) wr32(segment + offset, value + relocbase);
      }
    }
  }
  if (i < data0Size - 12) {
    for (m = 0; m < 3; m++) {
      count = be32(data0 + i); i += 4;
      if (count) { debug(DEBUG_ERROR, "APP", "code xrefs not supported"); break; }
    }
  }
  (void)code1_xrefs;
  return 0;
}

uint32_t app_launch(void) {
  DmOpenRef db;
  MemHandle hCode0, hCode1, hData0, hData, hStack, hInfo;
  uint8_t *code0, *code1, *data0, *data, *stack, *info;
  uint32_t codeStart, codeSize, aboveSize, dataSize, dataStart;
  r68k_state S;
  emu_state_t *state = m68k_get_emu_state();

  if ((db = storage_open_app("BikeOrDie")) == NULL) return 1;
  hCode0 = DmGet1Resource('code', 0);
  hCode1 = DmGet1Resource('code', 1);
  hData0 = DmGet1Resource('data', 0);
  if (!hCode1) { debug(DEBUG_ERROR, "APP", "no code 1"); return 1; }
  code0 = hCode0 ? MemHandleLock(hCode0) : NULL;
  code1 = MemHandleLock(hCode1);
  data0 = hData0 ? MemHandleLock(hData0) : NULL;
  codeStart = (uint32_t)code1;
  codeSize = MemHandleSize(hCode1);

  hInfo = MemHandleNew(sizeof(SysAppInfoType) + 16);
  info = MemHandleLock(hInfo);
  wr16((uint32_t)info, sysAppLaunchCmdNormalLaunch);
  wr32((uint32_t)info + 2, 0);
  wr16((uint32_t)info + 6, sysAppLaunchFlagNewGlobals | sysAppLaunchFlagDataRelocated);
  wr32((uint32_t)info + 12, (uint32_t)hCode1);
  state->sysAppInfoStart = (uint32_t)info;

  if (code0) {
    aboveSize = be32(code0);
    dataSize = be32(code0 + 4);
    hData = MemHandleNew(dataSize + aboveSize);
    data = MemHandleLock(hData);
    dataStart = (uint32_t)data;
    debug(DEBUG_INFO, "APP", "globals: dataSize %u aboveSize %u at 0x%08X", dataSize, aboveSize, dataStart);
    if (data0 && load_globals(data0, MemHandleSize(hData0), data, dataSize, dataStart, codeStart)) return 1;
  } else {
    dataSize = aboveSize = 0; dataStart = 0; data = NULL;
  }

  hStack = MemHandleNew(STACK_SIZE);
  stack = MemHandleLock(hStack);
  state->stackStart = (uint32_t)stack;

  memset(&S, 0, sizeof S);
  S.pc = codeStart;
  S.a[5] = dataStart + dataSize;
  S.a[7] = (uint32_t)stack + STACK_SIZE - 16;
  S.ret_sp = 0xFFFFFFFFu;
  r68k_segbase[1] = codeStart;
  r68k_segsize[1] = codeSize;
  debug(DEBUG_INFO, "APP", "code 1 at 0x%08X size 0x%X, a5=0x%08X a7=0x%08X", codeStart, codeSize, S.a[5], S.a[7]);
  r68k_run(&S);
  debug(DEBUG_INFO, "APP", "application finished at 0x%08X (%s)", S.pc, state->panic ? state->panic : "ok");
  return 0;
}
