/* The GBA platform layer's own interfaces. */
#ifndef BOD_H
#define BOD_H
#include <stdint.h>
void storage_init(void);
void storage_load(void);
void storage_sync(void);
DmOpenRef storage_open_app(const char *name);
DmOpenRef storage_app_db(void);
void storage_set_app_db(DmOpenRef db);
void pumpkin_load_fonts(void);
void input_poll(void);
void screen_copy_rect(const uint8_t *bits, int rowBytes, int x, int y, int w, int h);
void screen_set_palette(const RGBColorType *table, int n);
void platform_init(void);
BitmapType *bod_indirect_bitmap(Coord width, Coord height, UInt16 rowBytes, UInt8 depth, Boolean transparent, void *bits);
BitmapType *bod_vram_bitmap(Coord x, Coord y, Coord w, Coord h);
WinHandle bod_vram_window(Coord x, Coord y, Coord w, Coord h);
uint32_t app_launch(void);
void bod_engine_install(void);
void bod_bmp_cache_flush(void);
#endif
