/* Sound: nothing plays yet. The stream API is accepted so the game keeps
 * its mixer state consistent; the callback is never invoked. */
#include <PalmOS.h>
#include "sys.h"
#include "pumpkin.h"
#include "debug.h"
#include "gba.h"

Err SndDoCmd(void *channelP, SndCommandPtr cmdP, Boolean noWait) { (void)channelP; (void)cmdP; (void)noWait; return errNone; }
void SndPlaySystemSound(SndSysBeepType beepID) { (void)beepID; }
Err SndPlayResource(SndPtr sndP, Int32 volume, UInt32 flags) { (void)sndP; (void)volume; (void)flags; return errNone; }
Err SndPlaySmf(void *chanP, SndSmfCmdEnum cmd, UInt8 *smfP, SndSmfOptionsType *selP, SndSmfChanRangeType *chanRangeP, SndSmfCallbacksType *callbacksP, Boolean bNoWait) {
  (void)chanP; (void)cmd; (void)smfP; (void)selP; (void)chanRangeP; (void)callbacksP; (void)bNoWait; return errNone;
}
Err SndPlaySmfResource(UInt32 resType, Int16 resID, SystemPreferencesChoice volumeSelector) { (void)resType; (void)resID; (void)volumeSelector; return errNone; }
Boolean SndCreateMidiList(UInt32 creator, Boolean multipleDBs, UInt16 *wCountP, MemHandle *entHP) { (void)creator; (void)multipleDBs; if (wCountP) *wCountP = 0; if (entHP) *entHP = NULL; return false; }
void SndGetDefaultVolume(UInt16 *uiVolumeP, UInt16 *sysVolumeP, UInt16 *gameVolumeP) {
  if (uiVolumeP) *uiVolumeP = sndMaxAmp; if (sysVolumeP) *sysVolumeP = sndMaxAmp; if (gameVolumeP) *gameVolumeP = sndMaxAmp;
}
void SndSetDefaultVolume(UInt16 *uiVolumeP, UInt16 *sysVolumeP, UInt16 *gameVolumeP) { (void)uiVolumeP; (void)sysVolumeP; (void)gameVolumeP; }
Err SndStreamCreate(SndStreamRef *channel, SndStreamMode mode, UInt32 samplerate, SndSampleType type, SndStreamWidth width, SndStreamBufferCallback func, void *userdata, UInt32 buffsize, Boolean armNative) {
  (void)mode; (void)samplerate; (void)type; (void)width; (void)func; (void)userdata; (void)buffsize; (void)armNative;
  if (channel) *channel = 1;
  return errNone;
}
Err SndStreamCreateExtended(SndStreamRef *channel, SndStreamMode mode, SndFormatType format, UInt32 samplerate, SndSampleType type, SndStreamWidth width, SndStreamVariableBufferCallback func, void *userdata, UInt32 buffsize, Boolean armNative) {
  (void)mode; (void)format; (void)samplerate; (void)type; (void)width; (void)func; (void)userdata; (void)buffsize; (void)armNative;
  if (channel) *channel = 1;
  return errNone;
}
Err SndStreamDelete(SndStreamRef channel) { (void)channel; return errNone; }
Err SndStreamSetVolume(SndStreamRef channel, Int32 volume) { (void)channel; (void)volume; return errNone; }
Err SndStreamStart(SndStreamRef channel) { (void)channel; return errNone; }
Err SndStreamStop(SndStreamRef channel) { (void)channel; return errNone; }
Err SndStreamPause(SndStreamRef channel, Boolean pause) { (void)channel; (void)pause; return errNone; }
Err SndStreamSetPan(SndStreamRef channel, Int32 panposition) { (void)channel; (void)panposition; return errNone; }
Err SndStreamGetVolume(SndStreamRef channel, Int32 *volume) { (void)channel; if (volume) *volume = 1024; return errNone; }
Err SndStreamGetPan(SndStreamRef channel, Int32 *panposition) { (void)channel; if (panposition) *panposition = 0; return errNone; }
Err SndStreamDeviceControl(SndStreamRef channel, Int32 cmd, void *param, Int32 size) { (void)channel; (void)cmd; (void)param; (void)size; return errNone; }

/* The ARM code creates its stream through the PACE syscall, which lands here. */
Err SndStreamCreateEx(SndStreamRef *channel, SndStreamMode mode, SndFormatType format, UInt32 samplerate, SndSampleType type, SndStreamWidth width,
                      SndStreamBufferCallback func, SndStreamVariableBufferCallback vfunc, void *userdata, UInt32 buffsize, Boolean armNative, Boolean m68k, Boolean arm) {
  (void)mode; (void)format; (void)type; (void)width; (void)vfunc; (void)buffsize; (void)armNative; (void)m68k; (void)arm;
  debug(DEBUG_INFO, "SND", "SndStreamCreateEx rate %u callback 0x%08X data 0x%08X: refused", samplerate, (uint32_t)func, (uint32_t)userdata);
  if (channel) *channel = 0;
  return sndErrOpen;
}
