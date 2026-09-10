/* Host services PumpkinOS expects, in the sizes the GBA can afford. */
#include <PalmOS.h>
#include "sys.h"
#include "mutex.h"
#include "thread.h"
#include "pumpkin.h"
#include "debug.h"
#include "logtrap.h"
#include "gba.h"
#include "libc.h"

/* ---- threads and mutexes: one thread, nothing to lock ---- */
mutex_t *mutex_create(char *name) { (void)name; return (mutex_t *)1; }
int mutex_destroy(mutex_t *m) { (void)m; return 0; }
int mutex_lock(mutex_t *m) { (void)m; return 0; }
int mutex_unlock(mutex_t *m) { (void)m; return 0; }
int thread_must_end(void) { return 0; }
int thread_get_handle(void) { return 1; }

/* ---- logtrap: trap names are not compiled in ---- */
char *logtrap_trapname(logtrap_t *lt, uint16_t trap, uint16_t *selector, int follow) { (void)lt; (void)trap; (void)follow; if (selector) *selector = 0; return NULL; }
int logtrap_started(logtrap_t *lt) { (void)lt; return 0; }

/* ---- pieces of Palm OS the game has no use for on a handheld console ---- */
#include <DLServer.h>
#include <LocaleMgr.h>
#include <OverlayMgr.h>
#include "language.h"
#include "emupalmos.h"

void mw_move(uint32_t a, uint32_t n, int host) { (void)a; (void)n; (void)host; }
uint8_t *emupalmos_ram(void) { return NULL; }
void *pumpkin_get_exception(void) { return NULL; }
UInt16 KbdGrfGetState(void) { return 0; }
language_t *LanguageGet(void) { return NULL; }
Err LmGetLocaleSetting(UInt16 iLocaleIndex, LmLocaleSettingChoice iChoice, void *oValue, UInt16 iValueSize) { (void)iLocaleIndex; (void)iChoice; (void)oValue; (void)iValueSize; return lmErrSettingDataOverflow; }
Err LmLocaleToIndex(const LmLocaleType *iLocale, UInt16 *oLocaleIndex) { (void)iLocale; if (oLocaleIndex) *oLocaleIndex = 0; return errNone; }
Err OmSetSystemLocale(const LmLocaleType *systemLocaleP) { (void)systemLocaleP; return errNone; }
void EvtEnableGraffiti(Boolean enable) { (void)enable; }
Err EvtFlushKeyQueue(void) { return errNone; }
Err EvtFlushNextPenStroke(void) { return errNone; }
Boolean EvtKeyQueueEmpty(void) { return true; }
Err EvtResetAutoOffTimer(void) { return errNone; }
Err EvtWakeup(void) { return errNone; }
Err DmCreateDatabaseFromImage(MemPtr bufferP) { (void)bufferP; return dmErrInvalidParam; }
Err DmDetachResource(DmOpenRef dbP, UInt16 index, MemHandle *oldHP) { (void)dbP; (void)index; if (oldHP) *oldHP = NULL; return dmErrInvalidParam; }
UInt16 DmFindSortPosition68K(DmOpenRef dbP, UInt32 newRecord, UInt32 newRecordInfo, UInt32 compar, Int16 other) { (void)dbP; (void)newRecord; (void)newRecordInfo; (void)compar; (void)other; return DmNumRecords(dbP); }
Err DmInsertionSort68K(DmOpenRef dbP, UInt32 comparF, Int16 other) { (void)dbP; (void)comparF; (void)other; return errNone; }
Err DmQuickSort68K(DmOpenRef dbP, UInt32 comparF, Int16 other) { (void)dbP; (void)comparF; (void)other; return errNone; }

/* the HotSync user name, which the registration code is keyed to */
Err DlkGetSyncInfo(UInt32 *succSyncDateP, UInt32 *lastSyncDateP, DlkSyncStateType *syncStateP, Char *nameBufP, Char *logBufP, Int32 *logLenP) {
  if (succSyncDateP) *succSyncDateP = 0;
  if (lastSyncDateP) *lastSyncDateP = 0;
  if (syncStateP) *syncStateP = dlkSyncStateNeverSynced;
  if (nameBufP) strcpy(nameBufP, "PalmDB");
  if (logBufP) logBufP[0] = 0;
  if (logLenP) *logLenP = 0;
  return errNone;
}

/* the VFS: there is no expansion card */
void palmos_filesystemtrap(uint32_t sp, uint16_t idx, uint32_t sel) {
  (void)sp; (void)idx;
  debug(DEBUG_INFO, "VFS", "FileSystemDispatch selector %d: no volumes", sel);
  m68k_set_reg(M68K_REG_D0, vfsErrVolumeBadRef);
}

/* ---- launching other applications and loading libraries: not here ---- */
Err SysAppLaunch(UInt16 cardNo, LocalID dbID, UInt16 launchFlags, UInt16 cmd, MemPtr cmdPBP, UInt32 *resultP) {
  (void)cardNo; (void)dbID; (void)launchFlags; (void)cmd; (void)cmdPBP; if (resultP) *resultP = 0; return sysErrParamErr;
}
void SysKeyboardDialog(KeyboardType kbd) { (void)kbd; }
void SysKeyboardDialogV10(void) {}
Boolean SysLibNewRefNum68K(UInt32 type, UInt32 creator, UInt16 *refNum) { (void)type; (void)creator; if (refNum) *refNum = 0; return false; }
Err SysLibRegister68K(UInt16 refNum, LocalID dbID, uint8_t *code, UInt32 size, UInt16 *dispatchTblP, UInt8 *globalsP) { (void)refNum; (void)dbID; (void)code; (void)size; (void)dispatchTblP; (void)globalsP; return sysErrLibNotFound; }
uint8_t *SysLibTblEntry68K(UInt16 refNum, SysLibTblEntryType *tbl) { (void)refNum; (void)tbl; return NULL; }
void SysLibCancelRefNum68K(UInt16 refNum) { (void)refNum; }
UInt16 SysLibFind68K(char *name) { (void)name; return 0xFFFF; }
int thread_needs_run(void) { return 0; }
void thread_yield(int waiting) { (void)waiting; }
void mw_alloc(uint32_t a, uint32_t size) { (void)a; (void)size; }
void mw_free(uint32_t a) { (void)a; }
Err ExgDBReadARM(uint32_t readProc, uint32_t deleteProc, uint32_t userData, LocalID *dbID, Boolean *needReset, Boolean keepDates) {
  (void)readProc; (void)deleteProc; (void)userData; (void)dbID; (void)keepDates; if (needReset) *needReset = false; return exgErrNotSupported;
}
void mw_note_lock(MemHandle h, uint32_t ptr) { (void)h; (void)ptr; }
void mw_alloc_tag(uint32_t a, uint32_t size, const char *tag) { (void)a; (void)size; (void)tag; }
