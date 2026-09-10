/* GBA hardware definitions for the Bike or Die 2 port. */
#ifndef GBA_H
#define GBA_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;  typedef uint16_t u16; typedef uint32_t u32;
typedef int8_t   s8;  typedef int16_t  s16; typedef int32_t  s32;
typedef volatile u16 vu16; typedef volatile u32 vu32;

#define REG(a)          (*(vu16 *)(a))
#define REG32(a)        (*(vu32 *)(a))
#define REG_DISPCNT     REG(0x04000000)
#define REG_DISPSTAT    REG(0x04000004)
#define REG_VCOUNT      REG(0x04000006)
#define REG_BG2CNT      REG(0x0400000C)
#define REG_DMA3SAD     REG32(0x040000D4)
#define REG_DMA3DAD     REG32(0x040000D8)
#define REG_DMA3CNT     REG32(0x040000DC)
#define REG_DMA1SAD     REG32(0x040000BC)
#define REG_DMA1DAD     REG32(0x040000C0)
#define REG_DMA1CNT     REG32(0x040000C4)
#define REG_TM0CNT_L    REG(0x04000100)
#define REG_TM0CNT_H    REG(0x04000102)
#define REG_TM1CNT_L    REG(0x04000104)
#define REG_TM1CNT_H    REG(0x04000106)
#define REG_TM2CNT_L    REG(0x04000108)
#define REG_TM2CNT_H    REG(0x0400010A)
#define REG_KEYINPUT    REG(0x04000130)
#define REG_IE          REG(0x04000200)
#define REG_IF          REG(0x04000202)
#define REG_WAITCNT     REG(0x04000204)
#define REG_IME         REG(0x04000208)
#define REG_SOUNDCNT_L  REG(0x04000080)
#define REG_SOUNDCNT_H  REG(0x04000082)
#define REG_SOUNDCNT_X  REG(0x04000084)
#define REG_FIFO_A      REG32(0x040000A0)

#define PALRAM          ((vu16 *)0x05000000)
#define VRAM            ((vu16 *)0x06000000)
#define VRAM_PAGE0      ((u8 *)0x06000000)
#define VRAM_PAGE1      ((u8 *)0x0600A000)
#define SRAM            ((volatile u8 *)0x0E000000)

#define DCNT_MODE3      3
#define DCNT_MODE4      4
#define DCNT_PAGE       (1 << 4)
#define DCNT_BG2        (1 << 10)
#define DSTAT_VBL_IRQ   (1 << 3)
#define IRQ_VBLANK      1
#define IRQ_TIMER0      (1 << 3)
#define IRQ_TIMER1      (1 << 4)

#define KEY_A      1
#define KEY_B      2
#define KEY_SELECT 4
#define KEY_START  8
#define KEY_RIGHT  16
#define KEY_LEFT   32
#define KEY_UP     64
#define KEY_DOWN   128
#define KEY_R      256
#define KEY_L      512
#define KEYS_HELD()  ((~REG_KEYINPUT) & 0x3FF)

#define RGB15(r, g, b)  ((u16)((r) | ((g) << 5) | ((b) << 10)))

#define DMA_ENABLE      (1u << 31)
#define DMA_32          (1u << 26)
#define DMA_REPEAT      (1u << 25)
#define DMA_SRC_FIXED   (2u << 23)
#define DMA_DST_RELOAD  (3u << 21)
#define DMA_AT_FIFO     (3u << 28)
#define DMA_IRQ         (1u << 30)

/* Section placement helpers. IWRAM code must be ARM code. */
#define IWRAM_CODE __attribute__((section(".iwram"), noinline, target("arm")))
#define IWRAM_DATA __attribute__((section(".iwram")))
#define IWRAM_BSS  __attribute__((section(".iwram_bss")))
#define ARM_CODE   __attribute__((target("arm")))

extern volatile u32 vblank_count;
extern volatile u32 irq_seen;
extern void (*irq_hook)(u32 flags);

/* mGBA debug port: printf-style messages that the emulator shows in its log.
 * Level 0 = fatal .. 4 = debug. Harmless on real hardware. */
void mgba_open(void);
void mgba_log(int level, const char *fmt, ...);

static inline void dma3_copy32(void *dst, const void *src, u32 words) {
  REG_DMA3SAD = (u32)src; REG_DMA3DAD = (u32)dst; REG_DMA3CNT = DMA_ENABLE | DMA_32 | words;
}
static inline void dma3_copy16(void *dst, const void *src, u32 halfwords) {
  REG_DMA3SAD = (u32)src; REG_DMA3DAD = (u32)dst; REG_DMA3CNT = DMA_ENABLE | halfwords;
}
static inline void vsync(void) {
  while (REG_VCOUNT >= 160) ;
  while (REG_VCOUNT < 160) ;
}
#endif
