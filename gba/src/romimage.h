/* Layout of the ROM data image written by tools/gba/packdata.py. */
#ifndef ROMIMAGE_H
#define ROMIMAGE_H
#include <stdint.h>

#define ROMIMAGE_BASE 0x08400000u
#define ROMIMAGE_MAGIC 0x52444F42u   /* 'BODR' little-endian */
#define ROMDB_RESOURCE 1

typedef struct {
  char name[32];
  uint32_t type, creator;
  uint16_t attrs, version;
  uint32_t crDate, modDate;
  uint32_t nentries;
  uint32_t entries;      /* address of the entry table */
  uint32_t flags;        /* ROMDB_RESOURCE */
  uint32_t appinfo;      /* address of the appInfo block data, or 0 */
  uint32_t seed;
  uint8_t pad[96 - 32 - 4*10];
} rom_db_t;

typedef struct { uint32_t type; uint16_t id; uint16_t pad; uint32_t chunk; } rom_res_t;
typedef struct { uint32_t uidattr; uint32_t chunk; uint32_t pad; } rom_rec_t;

typedef struct {
  uint32_t magic, version, ndbs, reserved;
  rom_db_t dbs[];
} rom_image_t;
#endif
