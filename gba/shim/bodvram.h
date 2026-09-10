/* Byte stores that are safe for the GBA's VRAM, which turns a byte write into
 * a halfword write of the byte doubled. Everything else is a plain store. */
#ifndef BODVRAM_H
#define BODVRAM_H
#include <stdint.h>
static inline void bod_put8(uint8_t *bits, uint32_t offset, uint8_t b) {
  uint8_t *p = bits + offset;
  if (((uint32_t)p >> 24) == 0x06) {
    volatile uint16_t *hw = (volatile uint16_t *)((uint32_t)p & ~1u);
    *hw = ((uint32_t)p & 1) ? (uint16_t)((*hw & 0x00FF) | (b << 8)) : (uint16_t)((*hw & 0xFF00) | b);
  } else {
    *p = b;
  }
}
#endif
