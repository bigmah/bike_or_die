/* Prototypes of the 68k<->host structure encoders in gba/pk/emulation/encdec.inc. */
#ifndef ENCDEC_H
#define ENCDEC_H
void encode_string(uint32_t stringP, char *buf, uint32_t len);
void decode_rgb(uint32_t rgbP, RGBColorType *rgb);
void encode_rgb(uint32_t rgbP, RGBColorType *rgb);
void decode_locale(uint32_t localeP, LmLocaleType *locale);
void encode_locale(uint32_t localeP, LmLocaleType *locale);
void decode_datetime(uint32_t dateTimeP, DateTimeType *dateTime);
void encode_datetime(uint32_t dateTimeP, DateTimeType *dateTime);
void decode_event(uint32_t eventP, EventType *event);
void encode_event(uint32_t eventP, EventType *event);
void encode_notify(uint32_t notifyP, SysNotifyParamType *notify);
void decode_notify(uint32_t notifyP, SysNotifyParamType *notify);
void decode_rectangle(uint32_t rP, RectangleType *rect);
void encode_rectangle(uint32_t rP, RectangleType *rect);
void decode_point(uint32_t pP, PointType *point);
void encode_point(uint32_t pP, PointType *point);
void decode_appinfo(uint32_t appInfoP, AppInfoType *appInfo);
void encode_appinfo(uint32_t appInfoP, AppInfoType *appInfo);
void encode_gadget(uint32_t gadgetP, FormGadgetType *gadget);
void encode_deviceinfo(uint32_t deviceInfoP, DeviceInfoType *deviceInfo);
void decode_FileInfoType(uint32_t fileInfoP, FileInfoType *fileInfo);
void encode_FileInfoType(uint32_t fileInfoP, FileInfoType *fileInfo);
void encode_VolumeInfoType(uint32_t volInfoP, VolumeInfoType *volInfo);
void decode_NetConfigNameType(uint32_t nameArrayP, NetConfigNameType *nameArray);
void encode_NetConfigNameType(uint32_t nameArrayP, NetConfigNameType *nameArray);
void decode_NetSocketAddrType(uint32_t addrP, NetSocketAddrType *a);
void encode_NetSocketAddrType(uint32_t addrP, NetSocketAddrType *a);
void encode_NetHostInfoBufType(uint32_t bufP, NetHostInfoBufType *buf);
void decode_smfoptions(uint32_t selP, SndSmfOptionsType *options);
void decode_sndcmd(uint32_t cmdP, SndCommandType *cmd);
#endif
