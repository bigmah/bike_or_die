/* The pieces of PumpkinOS's emupalmos.c the GBA build keeps: the emulator
 * state the trap handlers look at, panic/finish, and the 68k<->host structure
 * encoders (verbatim, in encdec.inc). */
#include <PalmOS.h>
#include <VFSMgr.h>
#include <DLServer.h>
#include <Helper.h>
#include <CharAttr.h>
#include <HsNavCommon.h>
#include <NetMgr.h>

#include "sys.h"
#include "mutex.h"
#include "storage.h"
#include "pumpkin.h"
#include "bytes.h"
#include "logtrap.h"
#include "emupalmos.h"
#include "debug.h"

static emu_state_t emu_state;

emu_state_t *m68k_get_emu_state(void) { return &emu_state; }

void emupalmos_finish(int f) { emu_state.finish = f; emu_state.m68k_state.finish = f; if (r68k_cur) r68k_cur->halt = 1; }
int emupalmos_finished(void) { return emu_state.finish; }

void emupalmos_panic(char *msg, int code) {
  debug(DEBUG_ERROR, "EmuPalmOS", "panic (%d): %s", code, msg);
  if (!emu_state.panic) emu_state.panic = msg;
  emupalmos_finish(1);
}

int emupalmos_check_address(uint32_t address, uint32_t size, int read) { (void)address; (void)size; (void)read; return 1; }

#include "encdec.inc"
