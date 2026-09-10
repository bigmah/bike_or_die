/* The level's scanline edge table, packed.
 *
 * The engine rasterises every level object into a table with one row per
 * world band: a count, then (x, id) pairs sorted by x. It sizes rows for
 * ten entries, rasterises, and if any row overflowed frees the table and
 * tries again with 1.5x the capacity; the tutorial ends up at 33 entries
 * per row, 184 KB, for an average of 13 in use. This runs the same loop as
 * two passes instead: the first counts entries per row (the patched insert
 * only bumps a counter), the second gets rows of exactly that size.
 *
 * Layout of the packed table (see tools/gba/patches/armc3.s and armc2.s):
 *   u32 offsets[rows]            byte offset of each row's count word
 *   per row: u16 cap, u16 count, entries[cap]
 * The engine's header at ctx+0x458c: [0] first row, [4] last row,
 * [8] stride (here: the counter array during a counting pass, else 0),
 * [0xc] size to clear (0: the runtime clears), [0x10] table, [0x14]
 * capacity (never reached), [0x18] overflow flag (1 forces the retry).
 *
 * Rows are world bands of 2^shift units; the engine sets the shift to 3 at
 * start-up and never touches it again. A level whose table would not leave
 * room for the rest of the load gets coarser bands: the table shrinks by
 * about half per step, at the price of edges quantised to 2, 4, ... pixels
 * vertically at full zoom. */
#include <PalmOS.h>
#include "sys.h"
#include "debug.h"
#include "heap.h"
#include "libc.h"
#include "bod.h"

#define SHIFT_DEFAULT 3
#define SHIFT_MAX     6
#define EDGE_RESERVE  60000    /* what the rest of the level load still needs */

static uint32_t *counts;
static uint32_t counts_rows;

static uint32_t counting_pass(uint32_t *tbl, uint32_t rows) {
  if (counts) { heap_free(counts); counts = NULL; }
  if ((counts = heap_alloc_tag(rows * 4, "edgecnt")) == NULL) { tbl[2] = 0; tbl[3] = 0; tbl[4] = 0; tbl[6] = 0; return 0; }
  counts_rows = rows;
  tbl[2] = (uint32_t)counts;
  tbl[3] = 0;
  tbl[4] = (uint32_t)MemPtrNew(4);    /* something for the engine to free before the next pass */
  tbl[5] = 0x7fff;
  tbl[6] = 1;
  return 0;
}

uint32_t bod_edge_hook(uint32_t ctx) {
  uint32_t *tbl = (uint32_t *)(ctx + 0x458c), *shift = (uint32_t *)(ctx + 0x4420);
  int32_t first = (int32_t)tbl[0], last = (int32_t)tbl[1];
  uint32_t rows = (uint32_t)(last - first + 1), r, total, pos, free_bytes, largest, used;
  uint8_t *data;
  uint32_t *offsets;
  debug(DEBUG_INFO, "EDGE", "hook: rows %d..%d capacity %u shift %u", first, last, tbl[5], *shift);
  if (last < first || rows > 65535) { tbl[4] = 0; tbl[6] = 0; return 0; }
  if (tbl[5] == 10 && *shift != SHIFT_DEFAULT) {
    /* a new level: start from the default band size */
    *shift = SHIFT_DEFAULT;
    return counting_pass(tbl, rows);
  }
  if (tbl[5] == 10 || !counts || counts_rows != rows) return counting_pass(tbl, rows);
  total = rows * 4;
  for (r = 0; r < rows; r++) total += 4 + 4 * counts[r];
  heap_stats(&free_bytes, &largest, &used);
  if (total + EDGE_RESERVE > largest && *shift < SHIFT_MAX) {
    debug(DEBUG_INFO, "EDGE", "%u-byte table with %u free: coarser bands", total, largest);
    *shift += 1;
    return counting_pass(tbl, rows);
  }
  if ((data = MemPtrNew(total)) == NULL) {
    debug(DEBUG_ERROR, "EDGE", "no memory for a %u-byte table (%u rows)", total, rows);
    heap_free(counts); counts = NULL;
    tbl[2] = 0; tbl[3] = 0; tbl[4] = 0; tbl[6] = 0;
    return 0;
  }
  offsets = (uint32_t *)data;
  pos = rows * 4;
  for (r = 0; r < rows; r++) {
    *(uint16_t *)(data + pos) = counts[r];
    offsets[r] = pos + 2;
    pos += 4 + 4 * counts[r];
  }
  debug(DEBUG_INFO, "EDGE", "%u rows, %u entries, %u bytes, shift %u", rows, (total - rows * 4) / 4 - rows, total, *shift);
  heap_free(counts); counts = NULL;
  tbl[2] = 0; tbl[3] = 0; tbl[4] = (uint32_t)data; tbl[5] = 0x7fff; tbl[6] = 0;
  return 0;
}
