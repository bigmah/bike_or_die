/* The engine's hot code in IWRAM.
 *
 * The patched `armc` resources carry a block of code assembled for a fixed
 * IWRAM address (tools/gba/armpatch.py, `@@ iwram` blocks), with a footer
 * naming it and the words inside it that hold offsets into the resource.
 * Copy each block in and add the resource's address to those words; the
 * patched code in ROM jumps to the block by absolute address. */
#include <PalmOS.h>
#include "sys.h"
#include "debug.h"
#include "heap.h"
#include "libc.h"
#include "bod.h"

static int installed;

static void install(uint16_t id) {
  MemHandle h = DmGet1Resource('armc', id);
  const uint8_t *res;
  const uint32_t *foot;
  uint32_t size, blk, len, nrel, dest, i;
  if (!h) return;
  res = MemHandleLock(h);
  size = MemPtrSize((void *)res);
  if (size < 20) return;
  foot = (const uint32_t *)(res + size - 20);
  if (foot[0] != 0x4D525749) return;   /* 'IWRM' */
  blk = foot[1]; len = foot[2]; nrel = foot[3]; dest = foot[4];
  if (dest < 0x03002800 || dest + len > 0x03005400) { debug(DEBUG_ERROR, "ENGINE", "armc %d: bad IWRAM block", id); return; }
  memcpy((void *)dest, res + blk, len);
  for (i = 0; i < nrel; i++) {
    uint32_t off = ((const uint32_t *)(res + blk + ((len + 3) & ~3u)))[i];
    *(uint32_t *)(dest + off) += (uint32_t)res;
  }
  debug(DEBUG_INFO, "ENGINE", "armc %d at 0x%08X (%u bytes): %u bytes at 0x%08X, %u relocations", id, (uint32_t)res, size, len, dest, nrel);
}

void bod_engine_install(void) {
  if (installed) return;
  installed = 1;
  install(2);
  install(3);
}
