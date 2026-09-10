/* The 68k trap dispatcher: PumpkinOS's palmos_systrap(), less the parts that
 * belong to the interpreter. The per-manager marshalling lives in the copied
 * *SysTrap.c files (see tools/gba/sync_pumpkin.sh). */
#include <PalmOS.h>
#include <VFSMgr.h>
#include <DLServer.h>
#include <Helper.h>
#include <CharAttr.h>
#include <HsNavCommon.h>

#include "sys.h"
#include "mutex.h"
#include "storage.h"
#include "pumpkin.h"
#include "bytes.h"
#include "logtrap.h"
#include "emupalmos.h"
#include "debug.h"
#include "sc_prot.h"

/* Time spent per trap, for the profile of the level load. */
struct bod_trap_time { uint16_t trap; uint16_t n; uint32_t usec; } bod_trap_time[48];
static uint32_t palmos_systrap_1(uint16_t trap);
extern int64_t sys_get_clock(void);
uint32_t palmos_systrap(uint16_t trap) {
  int64_t t0 = sys_get_clock();
  uint32_t r = palmos_systrap_1(trap);
  uint32_t dt = (uint32_t)(sys_get_clock() - t0);
  int i;
  for (i = 0; i < 48; i++) {
    if (bod_trap_time[i].trap == trap || bod_trap_time[i].trap == 0) { bod_trap_time[i].trap = trap; bod_trap_time[i].n++; bod_trap_time[i].usec += dt; break; }
  }
  return r;
}

static uint32_t palmos_systrap_1(uint16_t trap) {
  uint32_t sp;
  uint16_t idx, selector;
  char buf[256], screator[8];
  emu_state_t *state = m68k_get_emu_state();
  uint32_t r = 0;

  trap = (trap & 0x0FFF) | 0xA000;
  sp = m68k_get_reg(NULL, M68K_REG_SP);
  idx = 0;
  (void)screator; (void)selector;

  switch (trap) {
    case sysTrapFileSystemDispatch:
      palmos_filesystemtrap(sp, idx, m68k_get_reg(NULL, M68K_REG_D2));
      break;
    case sysTrapHighDensityDispatch:
      palmos_highdensitytrap(sp, idx, m68k_get_reg(NULL, M68K_REG_D2));
      break;
    case sysTrapPinsDispatch:
      palmos_pinstrap(sp, idx, m68k_get_reg(NULL, M68K_REG_D2));
      break;
    case sysTrapNavSelector:
      selector = ARG16;
      palmos_navtrap(sp, idx, selector);
      break;
    case sysTrapOEMDispatch:
    case sysTrapOEMDispatch2:
      /* Sony / Handspring extensions: not this device. */
      m68k_set_reg(M68K_REG_D0, sysErrNotAllowed);
      break;

#include "systrap_cases.inc"
#include "sc_case.c"

    default:
      if (trap >= sysLibTrapName) {
        /* A system library call (NetLib): none is loadable here. */
        debug(DEBUG_ERROR, "EmuPalmOS", "library trap 0x%04X ignored", trap);
        m68k_set_reg(M68K_REG_D0, sysErrLibNotFound);
      } else {
        sys_snprintf(buf, sizeof(buf)-1, "trap 0x%04X not mapped", trap);
        emupalmos_panic(buf, EMUPALMOS_INVALID_TRAP);
      }
      break;
  }
  (void)state;
  return r;
}
