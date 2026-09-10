/* First-fit allocator with coalescing over one contiguous EWRAM region. */
#include "heap.h"
#include "libc.h"
#include "gba.h"

extern uint8_t __eheap_start[], __eheap_end[];

#define MAX_MASTERS 1024
static uint32_t masters[MAX_MASTERS];
static uint16_t master_free;            /* head of the free list, index + 1 */
static uint8_t *heap_lo, *heap_hi;
static chunk_t *rover;                  /* where the last allocation ended */
const char *heap_ctx = "platform";       /* who is allocating, for the dump */
uint32_t heap_trap;                      /* the 68k trap in progress, if any */

static inline chunk_t *next_chunk(chunk_t *c) {
  return (chunk_t *)((uint8_t *)c + sizeof(chunk_t) + c->size);
}

void heap_init(void) {
  chunk_t *c;
  int i;
  heap_lo = __eheap_start;
  heap_hi = __eheap_end;
  c = (chunk_t *)heap_lo;
  memset(c, 0, sizeof *c);
  c->size = (uint32_t)(heap_hi - heap_lo) - sizeof(chunk_t);
  c->magic = CHUNK_MAGIC;
  c->flags = CF_FREE | CF_LAST;
  rover = c;
  for (i = 0; i < MAX_MASTERS - 1; i++) masters[i] = i + 2;
  masters[MAX_MASTERS - 1] = 0;
  master_free = 1;
}

static uint32_t *master_alloc(void) {
  uint16_t i = master_free;
  if (!i) return NULL;
  master_free = (uint16_t)masters[i - 1];
  masters[i - 1] = 0;
  return &masters[i - 1];
}

static void master_release(uint32_t *m) {
  if (m >= masters && m < masters + MAX_MASTERS) {
    uint16_t i = (uint16_t)(m - masters) + 1;
    *m = master_free;
    master_free = i;
  }
}

static void split(chunk_t *c, uint32_t size) {
  uint32_t rest = c->size - size;
  if (rest >= sizeof(chunk_t) + 16) {
    chunk_t *n = (chunk_t *)((uint8_t *)c + sizeof(chunk_t) + size);
    memset(n, 0, sizeof *n);
    n->size = rest - sizeof(chunk_t);
    n->magic = CHUNK_MAGIC;
    n->flags = CF_FREE | (c->flags & CF_LAST);
    c->flags &= ~CF_LAST;
    c->size = size;
  }
}

static chunk_t *find(uint32_t size, chunk_t *from) {
  chunk_t *c = from;
  for (;;) {
    if ((c->flags & CF_FREE) && c->size >= size) return c;
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
  return NULL;
}

/* The last free chunk that fits: small, short-lived objects go at the top of
 * the heap, so the big long-lived buffers below stay contiguous. */
static chunk_t *find_last(uint32_t size) {
  chunk_t *c = (chunk_t *)heap_lo, *best = NULL;
  for (;;) {
    if ((c->flags & CF_FREE) && c->size >= size) best = c;
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
  return best;
}

/* Split so that the allocation sits at the END of a free chunk. */
static chunk_t *split_top(chunk_t *c, uint32_t size) {
  uint32_t rest = c->size - size;
  if (rest >= sizeof(chunk_t) + 16) {
    chunk_t *n = (chunk_t *)((uint8_t *)c + sizeof(chunk_t) + rest - sizeof(chunk_t));
    memset(n, 0, sizeof *n);
    n->size = size;
    n->magic = CHUNK_MAGIC;
    n->flags = CF_FREE | (c->flags & CF_LAST);
    c->flags &= ~CF_LAST;
    c->size = rest - sizeof(chunk_t);
    return n;
  }
  return c;
}

void *heap_alloc(uint32_t size) { return heap_alloc_tag(size, NULL); }

void *heap_alloc_tag(uint32_t size, const char *tag) {
  chunk_t *c;
  uint32_t *m;
  size = (size + 3) & ~3u;
  if (size == 0) size = 4;
  if (size <= 512) {
    c = find_last(size);
    if (c) c = split_top(c, size);
  } else {
    c = find(size, (chunk_t *)heap_lo);
  }
  if (!c) {
    uint32_t f, l, u;
    heap_stats(&f, &l, &u);
    mgba_log(1, "heap: out of memory for %u bytes (%s) (free %u largest %u chunks %u)", size, tag ? tag : "-", f, l, u);
    heap_dump();
    return NULL;
  }
  if ((m = master_alloc()) == NULL) { mgba_log(1, "heap: out of masters"); return NULL; }
  if (size > 512) split(c, size);
  if (size >= 8192) { extern uint32_t arm_syscall_lr; mgba_log(3, "heap: big alloc %u (%s) trap 0x%04X arm lr 0x%08X at %p", size, tag ? tag : "-", heap_trap, arm_syscall_lr, CHUNK_DATA(c)); }
  c->flags &= ~CF_FREE;
  c->type = 0; c->id = 0; c->attr = 0; c->lock = 0;
  if (!tag) tag = heap_ctx;
  if (tag) { c->type = (uint32_t)tag; c->flags |= CF_TAG; }
  c->master = m;
  *m = (uint32_t)CHUNK_DATA(c);
  rover = (c->flags & CF_LAST) ? (chunk_t *)heap_lo : next_chunk(c);
  memset(CHUNK_DATA(c), 0, size);
  return CHUNK_DATA(c);
}

static void coalesce_all(void) {
  chunk_t *c = (chunk_t *)heap_lo;
  for (;;) {
    if (c->flags & CF_LAST) break;
    chunk_t *n = next_chunk(c);
    if ((c->flags & CF_FREE) && (n->flags & CF_FREE)) {
      c->size += sizeof(chunk_t) + n->size;
      c->flags |= n->flags & CF_LAST;
      continue;
    }
    c = n;
  }
  rover = (chunk_t *)heap_lo;
}

void heap_free(void *p) {
  chunk_t *c;
  if (!p || IS_ROM(p)) return;
  c = CHUNK_OF(p);
  if (c->magic != CHUNK_MAGIC || (c->flags & CF_FREE)) { mgba_log(1, "heap: bad free %p", p); return; }
  master_release(c->master);
  c->master = NULL;
  c->flags |= CF_FREE;
  c->flags &= ~(CF_RES | CF_REC | CF_TAG);
  coalesce_all();
}

uint32_t heap_size(const void *p) {
  if (!p) return 0;
  return CHUNK_OF(p)->size;
}

void *heap_realloc(void *p, uint32_t size) {
  chunk_t *c, *n;
  void *q;
  uint32_t old;
  if (!p) return heap_alloc(size);
  if (IS_ROM(p)) return NULL;
  c = CHUNK_OF(p);
  size = (size + 3) & ~3u;
  if (size == 0) size = 4;
  old = c->size;
  if (size <= old) {
    split(c, size);
    coalesce_all();
    return p;
  }
  /* grow into a free neighbour */
  if (!(c->flags & CF_LAST)) {
    n = next_chunk(c);
    if ((n->flags & CF_FREE) && old + sizeof(chunk_t) + n->size >= size) {
      c->size = old + sizeof(chunk_t) + n->size;
      c->flags |= n->flags & CF_LAST;
      split(c, size);
      memset((uint8_t *)p + old, 0, size - old);
      return p;
    }
  }
  /* move; the master follows so the handle stays valid */
  if ((q = heap_alloc(size)) == NULL) return NULL;
  memcpy(q, p, old);
  {
    chunk_t *cq = CHUNK_OF(q);
    uint32_t *m = cq->master;
    master_release(m);
    cq->master = c->master;
    cq->type = c->type; cq->id = c->id; cq->attr = c->attr; cq->lock = c->lock;
    cq->flags |= c->flags & (CF_RES | CF_REC | CF_TAG);
    *cq->master = (uint32_t)q;
    c->master = NULL;
    c->flags |= CF_FREE;
    c->flags &= ~(CF_RES | CF_REC | CF_TAG);
    coalesce_all();
  }
  return q;
}

void heap_stats(uint32_t *free_bytes, uint32_t *largest, uint32_t *used_chunks) {
  chunk_t *c = (chunk_t *)heap_lo;
  uint32_t f = 0, l = 0, u = 0;
  for (;;) {
    if (c->flags & CF_FREE) { f += c->size; if (c->size > l) l = c->size; } else u++;
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
  if (free_bytes) *free_bytes = f;
  if (largest) *largest = l;
  if (used_chunks) *used_chunks = u;
}

int heap_check(const char *where) {
  chunk_t *c = (chunk_t *)heap_lo;
  int n = 0;
  for (;;) {
    if (c->magic != CHUNK_MAGIC || (uint8_t *)c + sizeof(chunk_t) + c->size > heap_hi) {
      mgba_log(1, "heap: corrupt at %p (%s) chunk %d", c, where, n);
      return -1;
    }
    n++;
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
  return 0;
}

void heap_histogram(void) {
  chunk_t *c = (chunk_t *)heap_lo;
  uint32_t buckets[8] = {0}, bytes[8] = {0};
  static const uint32_t lim[8] = { 16, 32, 64, 128, 256, 1024, 4096, 0xFFFFFFFF };
  int i;
  for (;;) {
    if (!(c->flags & CF_FREE)) { for (i = 0; i < 8; i++) if (c->size <= lim[i]) { buckets[i]++; bytes[i] += c->size; break; } }
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
  for (i = 0; i < 8; i++) mgba_log(3, "heap: <=%u: %u chunks, %u bytes", lim[i], buckets[i], bytes[i]);
}

void heap_dump(void) {
  chunk_t *c = (chunk_t *)heap_lo;
  for (;;) {
    if (!(c->flags & CF_FREE) && c->size >= 256) {
      if (c->flags & CF_TAG) mgba_log(3, "  chunk %p size %6u lock %d %s", CHUNK_DATA(c), c->size, c->lock, (const char *)c->type);
      else mgba_log(3, "  chunk %p size %6u lock %d flags %02x type %08x id %d", CHUNK_DATA(c), c->size, c->lock, c->flags, c->type, c->id);
    }
    if (c->flags & CF_LAST) break;
    c = next_chunk(c);
  }
}
