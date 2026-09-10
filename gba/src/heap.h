/* The dynamic heap: Palm OS chunks over EWRAM.
 *
 * Every chunk, in RAM or in the ROM data image, is preceded by the same
 * 24-byte header, so the memory manager can tell what it is looking at from
 * a bare pointer. A handle is the address of a master word holding the data
 * address: in ROM that word is the first field of the header, in RAM it lives
 * in a pool so a chunk can be moved by a resize without invalidating it. */
#ifndef BOD_HEAP_H
#define BOD_HEAP_H
#include <stdint.h>

typedef struct {
  uint32_t master_word;   /* ROM chunks: the master word itself */
  uint32_t type;          /* resource type, or 0 */
  uint16_t id;            /* resource id, or record attributes/uid */
  uint16_t attr;
  uint32_t size;          /* usable bytes */
  uint8_t  magic;         /* CHUNK_MAGIC */
  uint8_t  flags;
  uint16_t lock;
  uint32_t *master;       /* the handle */
} chunk_t;

#define CHUNK_MAGIC 0xA5
#define CF_FREE   0x01
#define CF_ROM    0x02
#define CF_RES    0x04   /* a resource: type/id are meaningful */
#define CF_REC    0x08   /* a database record */
#define CF_TAG    0x10   /* type holds a tag string for the dump */
#define CF_LAST   0x80   /* last chunk of the heap */

#define CHUNK_OF(p)   ((chunk_t *)((uint8_t *)(p) - sizeof(chunk_t)))
#define CHUNK_DATA(c) ((void *)((uint8_t *)(c) + sizeof(chunk_t)))
#define IS_ROM(p)     (((uint32_t)(p)) >= 0x08000000u)

extern const char *heap_ctx;
extern uint32_t heap_trap;
void     heap_init(void);
void    *heap_alloc(uint32_t size);          /* zeroed, with a master */
void    *heap_alloc_tag(uint32_t size, const char *tag);
void     heap_free(void *p);
void    *heap_realloc(void *p, uint32_t size); /* may move; the master follows */
uint32_t heap_size(const void *p);
void     heap_stats(uint32_t *free_bytes, uint32_t *largest, uint32_t *used_chunks);
int      heap_check(const char *where);
void     heap_dump(void);
void     heap_histogram(void);
#endif
