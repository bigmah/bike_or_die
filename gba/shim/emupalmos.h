/* GBA replacement for PumpkinOS's emulation/emupalmos.h: what the trap
 * marshalling sources need, without Musashi or the ARM interpreter. A 68k
 * address is a real address here, so the in/out conversions are identities. */
#ifndef EMUPALMOS_H
#define EMUPALMOS_H
#include <stdint.h>
#include <PalmOS.h>
#include <VFSMgr.h>
#include <NetMgr.h>
#include "m68k/m68k.h"
#include "logtrap.h"
#include "emupalmosinc.h"

#define READ_BYTE(BASE, ADDR) (BASE)[ADDR]
#define READ_WORD(BASE, ADDR) (((BASE)[ADDR]<<8) | (BASE)[(ADDR)+1])
#define READ_LONG(BASE, ADDR) (((BASE)[ADDR]<<24) | ((BASE)[(ADDR)+1]<<16) | ((BASE)[(ADDR)+2]<<8) | (BASE)[(ADDR)+3])
#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[ADDR] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>8) & 0xff; (BASE)[(ADDR)+1] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>24) & 0xff; (BASE)[(ADDR)+1] = ((VAL)>>16)&0xff; (BASE)[(ADDR)+2] = ((VAL)>>8)&0xff; (BASE)[(ADDR)+3] = (VAL)&0xff

#define ARG8  m68k_read_memory_8 (sp + idx); idx += 2
#define ARG16 m68k_read_memory_16(sp + idx); idx += 2
#define ARG32 m68k_read_memory_32(sp + idx); idx += 4

#define ARG64(a,b) (((uint64_t)(a)) << 32) | (b)
#define ARG_DOUBLE(a) \
    uint32_t a ## _high = ARG32; \
    uint32_t a ## _low  = ARG32; \
    flp_double_t a; \
    a.i = ARG64(a ## _high, a ## _low)
#define RES_DOUBLE(a,p) \
    m68k_write_memory_32(p,   (uint32_t)(a.i >> 32)); \
    m68k_write_memory_32(p+4, (uint32_t)(a.i)); \
    m68k_set_reg(M68K_REG_A0, p)

#define stackSize 4096

#define sysTrapFrmGetEventHandler68K   0xA500
#define sysTrapCtlGetStyle68K          0xA501
#define sysTrapFrmGetGadgetPtr68K      0xA502
#define sysTrapSysLibNewRefNum68K      0xA503
#define sysTrapSysLibRegister68K       0xA504
#define sysTrapSysLibCancelRefNum68K   0xA505
#define sysTrapPumpkinDebug            0xA473
#define sysTrapPumpkinDebugBytes       0xA474

typedef union { int32_t i; float f; } flp_float_t;
typedef union { int64_t i; double d; } flp_double_t;

typedef struct {
  int finish;
  uint32_t stackStart;
  uint32_t sysAppInfoStart;
  m68k_state_t m68k_state;
  char *panic;
  uint32_t SysFormPointerArrayToStrings_addr;
  uint32_t FrmDrawForm_addr;
  uint32_t SysQSort_addr;
  uint32_t SysBinarySearch_addr;
  uint32_t SysLibLoad_addr;
  MemHandle hNative;
  uint32_t screenStart;
  uint32_t screenEnd;
  int disasm;
  logtrap_t *lt;
} emu_state_t;

emu_state_t *m68k_get_emu_state(void);

uint32_t arm_native_call_pce(uint32_t code, uint32_t userData);

#include "encdec.h"
#include "notif_serde.h"
#include "emu_notif_serde.h"
#include "emu_launch_serde.h"

uint32_t palmos_systrap(uint16_t trap);
void palmos_highdensitytrap(uint32_t sp, uint16_t idx, uint32_t sel);
void palmos_pinstrap(uint32_t sp, uint16_t idx, uint32_t sel);
void palmos_filesystemtrap(uint32_t sp, uint16_t idx, uint32_t sel);
void palmos_navtrap(uint32_t sp, uint16_t idx, uint32_t sel);

static inline void *emupalmos_trap_in(uint32_t address, uint16_t trap, int arg) { (void)trap; (void)arg; return address ? (void *)address : (void *)0; }
static inline void *emupalmos_trap_sel_in(uint32_t address, uint16_t trap, uint16_t sel, int arg) { (void)trap; (void)sel; (void)arg; return address ? (void *)address : (void *)0; }
static inline uint32_t emupalmos_trap_out(void *address) { return (uint32_t)address; }

Err ExgDBReadARM(uint32_t readProc, uint32_t deleteProc, uint32_t userData, LocalID *dbID, Boolean *needReset, Boolean keepDates);
#endif
