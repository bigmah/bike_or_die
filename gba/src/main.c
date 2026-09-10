/* Bike or Die 2 on the Game Boy Advance: bring the platform up, then launch
 * the recompiled application. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "debug.h"
#include "emupalmos.h"
#include "gba.h"
#include "libc.h"
#include "mutex.h"
#include "storage.h"
#include "bod.h"

int main(void) {
  /* ROM 3/1 wait states with prefetch is what every cartridge supports; the port
   * runs its engine from ROM, so it asks for 2/1 and the 1-wait EWRAM the GBA
   * (but not the DS or Micro) accepts, which mGBA honours. */
  REG_WAITCNT = 0x431B;
  *(volatile uint32_t *)0x04000800 = 0x0E000020;
  mgba_open();
  platform_init();
  app_launch();
  mgba_log(3, "gbarun: exit");
  for (;;) vsync();
  return 0;
}
