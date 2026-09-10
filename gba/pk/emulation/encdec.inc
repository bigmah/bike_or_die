void encode_string(uint32_t stringP, char *buf, uint32_t len) {
  uint32_t i;

  for (i = 0; i < len-1 && buf[i]; i++) {
    m68k_write_memory_8(stringP + i, buf[i]);
  }
  m68k_write_memory_8(stringP + i, 0);
}

void decode_locale(uint32_t localeP, LmLocaleType *locale) {
  if (localeP && locale) {
    MemSet(locale, sizeof(LmLocaleType), 0);
    locale->language = m68k_read_memory_16(localeP + 0);
    locale->country  = m68k_read_memory_16(localeP + 2);
  }
}

void encode_locale(uint32_t localeP, LmLocaleType *locale) {
  if (localeP && locale) {
    m68k_write_memory_16(localeP + 0, locale->language);
    m68k_write_memory_16(localeP + 2, locale->country);
  }
}

void decode_datetime(uint32_t dateTimeP, DateTimeType *dateTime) {
  if (dateTimeP && dateTime) {
    MemSet(dateTime, sizeof(DateTimeType), 0);
    dateTime->second  = m68k_read_memory_16(dateTimeP +  0);
    dateTime->minute  = m68k_read_memory_16(dateTimeP +  2);
    dateTime->hour    = m68k_read_memory_16(dateTimeP +  4);
    dateTime->day     = m68k_read_memory_16(dateTimeP +  6);
    dateTime->month   = m68k_read_memory_16(dateTimeP +  8);
    dateTime->year    = m68k_read_memory_16(dateTimeP + 10);
    dateTime->weekDay = m68k_read_memory_16(dateTimeP + 12);
  }
}

void encode_datetime(uint32_t dateTimeP, DateTimeType *dateTime) {
  if (dateTimeP && dateTime) {
    m68k_write_memory_16(dateTimeP +  0, dateTime->second);
    m68k_write_memory_16(dateTimeP +  2, dateTime->minute);
    m68k_write_memory_16(dateTimeP +  4, dateTime->hour);
    m68k_write_memory_16(dateTimeP +  6, dateTime->day);
    m68k_write_memory_16(dateTimeP +  8, dateTime->month);
    m68k_write_memory_16(dateTimeP + 10, dateTime->year);
    m68k_write_memory_16(dateTimeP + 12, dateTime->weekDay);
  }
}

void decode_appinfo(uint32_t appInfoP, AppInfoType *appInfo) {
  UInt16 i, j, k;

  if (appInfoP && appInfo) {
    appInfo->renamedCategories = m68k_read_memory_16(appInfoP + 0);

    for (i = 0, k = 2; i < dmRecNumCategories; i++) {
      for (j = 0; j < dmCategoryLength; j++, k++) {
        appInfo->categoryLabels[i][j] = m68k_read_memory_8(appInfoP + k);
      }
    }
  }
}

void encode_appinfo(uint32_t appInfoP, AppInfoType *appInfo) {
  UInt16 i, j, k;

  if (appInfoP && appInfo) {
    m68k_write_memory_16(appInfoP + 0, appInfo->renamedCategories);

    for (i = 0, k = 2; i < dmRecNumCategories; i++) {
      for (j = 0; j < dmCategoryLength; j++, k++) {
        m68k_write_memory_8(appInfoP + k, appInfo->categoryLabels[i][j]);
      }
    }
  }
}

void decode_event(uint32_t eventP, EventType *event) {
  uint8_t *ram = pumpkin_heap_base();
  uint32_t a, i;
  char buf[256];

  MemSet(event, sizeof(EventType), 0);

  event->eType    = m68k_read_memory_16(eventP + 0);
  event->penDown  = m68k_read_memory_8 (eventP + 2);
  event->tapCount = m68k_read_memory_8 (eventP + 3);
  event->screenX  = m68k_read_memory_16(eventP + 4);
  event->screenY  = m68k_read_memory_16(eventP + 6);

  switch (event->eType) {
    case penDownEvent:
    case penMoveEvent:
    case nilEvent:
    case frmSaveEvent:
    case appStopEvent:
    case winDisplayChangedEvent:
    case appRaiseEvent:
      break;
    case keyDownEvent:
      event->data.keyDown.chr = m68k_read_memory_16(eventP + 8);
      event->data.keyDown.keyCode = m68k_read_memory_16(eventP + 10);
      event->data.keyDown.modifiers = m68k_read_memory_16(eventP + 12);
      break;
    case modKeyDownEvent:
    case modKeyUpEvent:
      event->data.keyDown.modifiers = m68k_read_memory_16(eventP + 12);
      break;
    case penUpEvent:
      event->data.penUp.start.x = m68k_read_memory_16(eventP + 8);
      event->data.penUp.start.y = m68k_read_memory_16(eventP + 10);
      event->data.penUp.end.x = m68k_read_memory_16(eventP + 12);
      event->data.penUp.end.y = m68k_read_memory_16(eventP + 14);
      break;
    case frmLoadEvent:
      event->data.frmLoad.formID = m68k_read_memory_16(eventP + 8);
      break;
    case frmOpenEvent:
      event->data.frmOpen.formID = m68k_read_memory_16(eventP + 8);
      break;
    case frmUpdateEvent:
      event->data.frmUpdate.formID = m68k_read_memory_16(eventP + 8);
      event->data.frmUpdate.updateCode = m68k_read_memory_16(eventP + 10);
      break;
    case frmCloseEvent:
      event->data.frmClose.formID = m68k_read_memory_16(eventP + 8);
      break;
    case frmTitleEnterEvent:
      event->data.frmTitleEnter.formID = m68k_read_memory_16(eventP + 8);
      break;
    case frmTitleSelectEvent:
      event->data.frmTitleSelect.formID = m68k_read_memory_16(eventP + 8);
      break;
    case menuEvent:
      event->data.menu.itemID = m68k_read_memory_16(eventP + 8);
      break;
    case fldEnterEvent:
      event->data.fldEnter.fieldID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.fldEnter.pField = a ? (FieldType *)(ram + a) : NULL;
      break;
    case fldChangedEvent:
      event->data.fldChanged.fieldID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.fldChanged.pField = a ? (FieldType *)(ram + a) : NULL;
      break;
    case ctlEnterEvent:
      event->data.ctlEnter.controlID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.ctlEnter.pControl = a ? (ControlType *)(ram + a) : NULL;
      break;
    case ctlExitEvent:
      event->data.ctlExit.controlID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.ctlExit.pControl = a ? (ControlType *)(ram + a) : NULL;
      break;
    case ctlSelectEvent:
      event->data.ctlSelect.controlID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.ctlSelect.pControl = a ? (ControlType *)(ram + a) : NULL;
      event->data.ctlSelect.on = m68k_read_memory_8(eventP + 14);
      event->data.ctlSelect.reserved1 = 0;
      event->data.ctlSelect.value = m68k_read_memory_16(eventP + 16);
      break;
    case ctlRepeatEvent:
      event->data.ctlRepeat.controlID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.ctlRepeat.pControl = a ? (ControlType *)(ram + a) : NULL;
      event->data.ctlRepeat.time = m68k_read_memory_32(eventP + 14);
      event->data.ctlRepeat.value = m68k_read_memory_16(eventP + 18);
      break;
    case lstEnterEvent:
      event->data.lstEnter.listID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.lstEnter.pList = a ? (ListType *)(ram + a) : NULL;
      event->data.lstEnter.selection = m68k_read_memory_16(eventP + 14);
      break;
    case lstExitEvent:
      event->data.lstExit.listID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.lstExit.pList = a ? (ListType *)(ram + a) : NULL;
      break;
    case lstSelectEvent:
      event->data.lstSelect.listID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.lstSelect.pList = a ? (ListType *)(ram + a) : NULL;
      event->data.lstSelect.selection = m68k_read_memory_16(eventP + 14);
      break;
    case popSelectEvent:
      event->data.popSelect.controlID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.popSelect.controlP = a ? (ControlType *)(ram + a) : NULL;
      event->data.popSelect.listID = m68k_read_memory_16(eventP + 14);
      a = m68k_read_memory_32(eventP + 16);
      event->data.popSelect.listP = a ? (ListType *)(ram + a) : NULL;
      event->data.popSelect.selection = m68k_read_memory_16(eventP + 20);
      event->data.popSelect.priorSelection = m68k_read_memory_16(eventP + 22);
      break;
    case sclEnterEvent:
      event->data.sclEnter.scrollBarID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.sclEnter.pScrollBar = a ? (ScrollBarType *)(ram + a) : NULL;
      break;
    case sclExitEvent:
      event->data.sclExit.scrollBarID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.sclExit.pScrollBar = a ? (ScrollBarType *)(ram + a) : NULL;
      event->data.sclExit.value = m68k_read_memory_16(eventP + 14);
      event->data.sclExit.newValue = m68k_read_memory_16(eventP + 16);
      break;
    case sclRepeatEvent:
      event->data.sclRepeat.scrollBarID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.sclRepeat.pScrollBar = a ? (ScrollBarType *)(ram + a) : NULL;
      event->data.sclRepeat.value = m68k_read_memory_16(eventP + 14);
      event->data.sclRepeat.newValue = m68k_read_memory_16(eventP + 16);
      event->data.sclRepeat.time = m68k_read_memory_32(eventP + 18);
      break;
    case frmGadgetEnterEvent:
      event->data.gadgetEnter.gadgetID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.gadgetEnter.gadgetP = a ? (FormGadgetType *)(ram + a) : NULL;
      break;
    case frmGadgetMiscEvent:
      event->data.gadgetMisc.gadgetID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.gadgetMisc.gadgetP = a ? (FormGadgetType *)(ram + a) : NULL;
      event->data.gadgetMisc.selector = m68k_read_memory_16(eventP + 14);
      a = m68k_read_memory_32(eventP + 16);
      event->data.gadgetMisc.dataP = a ? (ram + a) : NULL;
      break;
    case tblEnterEvent:
      event->data.tblEnter.tableID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.tblEnter.pTable = a ? (TableType *)(ram + a) : NULL;
      event->data.tblEnter.row = m68k_read_memory_16(eventP + 14);
      event->data.tblEnter.column = m68k_read_memory_16(eventP + 16);
      break;
    case tblExitEvent:
      event->data.tblExit.tableID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.tblExit.pTable = a ? (TableType *)(ram + a) : NULL;
      event->data.tblExit.row = m68k_read_memory_16(eventP + 14);
      event->data.tblExit.column = m68k_read_memory_16(eventP + 16);
      break;
    case tblSelectEvent:
      event->data.tblSelect.tableID = m68k_read_memory_16(eventP + 8);
      a = m68k_read_memory_32(eventP + 10);
      event->data.tblSelect.pTable = a ? (TableType *)(ram + a) : NULL;
      event->data.tblSelect.row = m68k_read_memory_16(eventP + 14);
      event->data.tblSelect.column = m68k_read_memory_16(eventP + 16);
      break;
    case winEnterEvent:
      a = m68k_read_memory_32(eventP + 8);
      event->data.winEnter.enterWindow = a ? (WinHandle)(ram + a) : NULL;
      a = m68k_read_memory_32(eventP + 12);
      event->data.winEnter.exitWindow = a ? (WinHandle)(ram + a) : NULL;
      break;
    case winExitEvent:
      a = m68k_read_memory_32(eventP + 8);
      event->data.winExit.enterWindow = a ? (WinHandle)(ram + a) : NULL;
      a = m68k_read_memory_32(eventP + 12);
      event->data.winExit.exitWindow = a ? (WinHandle)(ram + a) : NULL;
      break;
    default:
      if (event->eType < firstUserEvent /*|| event->eType > lastUserEvent*/) {
        sys_snprintf(buf, sizeof(buf)-1, "decode event %s (%d) incomplete", EvtGetEventName(event->eType), event->eType);
        emupalmos_panic(buf, EMUPALMOS_UNDECODED_EVENT);
      } else {
        // 68K PalmOS EventType is 24 bytes long
        for (i = 0; i < 16; i++) {
          event->data.buffer[i] = m68k_read_memory_8(eventP + 8 + i);
        }
      }
      break;
  }
}

void encode_gadget(uint32_t gadgetP, FormGadgetType *gadget) {
  if (gadgetP && gadget) {
    m68k_write_memory_16(gadgetP +  0, gadget->id);
    m68k_write_memory_16(gadgetP +  2, 0); // XXX attr
    m68k_write_memory_16(gadgetP +  4, gadget->rect.topLeft.x);
    m68k_write_memory_16(gadgetP +  6, gadget->rect.topLeft.y);
    m68k_write_memory_16(gadgetP +  8, gadget->rect.extent.x);
    m68k_write_memory_16(gadgetP + 10, gadget->rect.extent.y);
    m68k_write_memory_32(gadgetP + 12, gadget->m68k_data);
    m68k_write_memory_32(gadgetP + 16, gadget->m68k_handler);
  }
}

void encode_event(uint32_t eventP, EventType *event) {
  uint8_t *ram = pumpkin_heap_base();
  uint32_t a, i;
  char buf[256];

  m68k_write_memory_16(eventP + 0, event->eType);
  m68k_write_memory_8 (eventP + 2, event->penDown);
  m68k_write_memory_8 (eventP + 3, event->tapCount);
  m68k_write_memory_16(eventP + 4, event->screenX);
  m68k_write_memory_16(eventP + 6, event->screenY);

  switch (event->eType) {
    case penDownEvent:
    case penMoveEvent:
    case nilEvent:
    case frmSaveEvent:
    case appStopEvent:
    case winDisplayChangedEvent:
    case appRaiseEvent:
      break;
    case keyDownEvent:
      m68k_write_memory_16(eventP +  8, event->data.keyDown.chr);
      m68k_write_memory_16(eventP + 10, event->data.keyDown.keyCode);
      m68k_write_memory_16(eventP + 12, event->data.keyDown.modifiers);
      break;
    case modKeyDownEvent:
    case modKeyUpEvent:
      m68k_write_memory_16(eventP + 12, event->data.keyDown.modifiers);
      break;
    case penUpEvent:
      m68k_write_memory_16(eventP +  8, event->data.penUp.start.x);
      m68k_write_memory_16(eventP + 10, event->data.penUp.start.y);
      m68k_write_memory_16(eventP + 12, event->data.penUp.end.x);
      m68k_write_memory_16(eventP + 14, event->data.penUp.end.y);
      break;
    case frmLoadEvent:
      m68k_write_memory_16(eventP + 8, event->data.frmLoad.formID);
      break;
    case frmOpenEvent:
      m68k_write_memory_16(eventP + 8, event->data.frmOpen.formID);
      break;
    case frmUpdateEvent:
      m68k_write_memory_16(eventP +  8, event->data.frmUpdate.formID);
      m68k_write_memory_16(eventP + 10, event->data.frmUpdate.updateCode);
      break;
    case frmCloseEvent:
      m68k_write_memory_16(eventP + 8, event->data.frmClose.formID);
      break;
    case frmTitleEnterEvent:
      m68k_write_memory_16(eventP + 8, event->data.frmTitleEnter.formID);
      break;
    case frmTitleSelectEvent:
      m68k_write_memory_16(eventP + 8, event->data.frmTitleSelect.formID);
      break;
    case menuEvent:
      m68k_write_memory_16(eventP + 8, event->data.menu.itemID);
      break;
    case fldEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.fldEnter.fieldID);
      m68k_write_memory_32(eventP + 10, event->data.fldEnter.pField ? (uint32_t)((uint8_t *)event->data.fldEnter.pField - ram) : 0);
      break;
    case fldChangedEvent:
      m68k_write_memory_16(eventP +  8, event->data.fldChanged.fieldID);
      m68k_write_memory_32(eventP + 10, event->data.fldChanged.pField ? (uint32_t)((uint8_t *)event->data.fldChanged.pField - ram) : 0);
      break;
    case ctlEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.ctlEnter.controlID);
      m68k_write_memory_32(eventP + 10, event->data.ctlEnter.pControl ? (uint32_t)((uint8_t *)event->data.ctlEnter.pControl - ram) : 0);
      break;
    case ctlExitEvent:
      m68k_write_memory_16(eventP +  8, event->data.ctlExit.controlID);
      m68k_write_memory_32(eventP + 10, event->data.ctlExit.pControl ? (uint32_t)((uint8_t *)event->data.ctlExit.pControl - ram) : 0);
      break;
    case ctlSelectEvent:
      m68k_write_memory_16(eventP +  8, event->data.ctlSelect.controlID);
      m68k_write_memory_32(eventP + 10, event->data.ctlSelect.pControl ? (uint32_t)((uint8_t *)event->data.ctlSelect.pControl - ram) : 0);
      m68k_write_memory_8 (eventP + 14, event->data.ctlSelect.on ? 1 : 0);
      m68k_write_memory_8 (eventP + 15, 0); // reserved
      m68k_write_memory_16(eventP + 16, event->data.ctlSelect.value);
      break;
    case ctlRepeatEvent:
      m68k_write_memory_16(eventP +  8, event->data.ctlRepeat.controlID);
      m68k_write_memory_32(eventP + 10, event->data.ctlRepeat.pControl ? (uint32_t)((uint8_t *)event->data.ctlRepeat.pControl - ram) : 0);
      m68k_write_memory_32(eventP + 14, event->data.ctlRepeat.time);
      m68k_write_memory_16(eventP + 18, event->data.ctlRepeat.value);
      break;
    case lstEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.lstEnter.listID);
      m68k_write_memory_32(eventP + 10, event->data.lstEnter.pList ? (uint32_t)((uint8_t *)event->data.lstEnter.pList - ram) : 0);
      m68k_write_memory_16(eventP + 14, event->data.lstEnter.selection);
      break;
    case lstExitEvent:
      m68k_write_memory_16(eventP +  8, event->data.lstExit.listID);
      m68k_write_memory_32(eventP + 10, event->data.lstExit.pList ? (uint32_t)((uint8_t *)event->data.lstExit.pList - ram) : 0);
      break;
    case lstSelectEvent:
      m68k_write_memory_16(eventP +  8, event->data.lstSelect.listID);
      m68k_write_memory_32(eventP + 10, event->data.lstSelect.pList ? (uint32_t)((uint8_t *)event->data.lstSelect.pList - ram) : 0);
      m68k_write_memory_16(eventP + 14, event->data.lstSelect.selection);
      break;
    case popSelectEvent:
      m68k_write_memory_16(eventP +  8, event->data.popSelect.controlID);
      m68k_write_memory_32(eventP + 10, event->data.popSelect.controlP ? (uint32_t)((uint8_t *)event->data.popSelect.controlP - ram) : 0);
      m68k_write_memory_16(eventP + 14, event->data.popSelect.listID);
      m68k_write_memory_32(eventP + 16, event->data.popSelect.listP ? (uint32_t)((uint8_t *)event->data.popSelect.listP - ram) : 0);
      m68k_write_memory_16(eventP + 20, event->data.popSelect.selection);
      m68k_write_memory_16(eventP + 22, event->data.popSelect.priorSelection);
      break;
    case sclEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.sclEnter.scrollBarID);
      m68k_write_memory_32(eventP + 10, event->data.sclEnter.pScrollBar ? (uint32_t)((uint8_t *)event->data.sclEnter.pScrollBar - ram) : 0);
      break;
    case sclExitEvent:
      m68k_write_memory_16(eventP +  8, event->data.sclExit.scrollBarID);
      m68k_write_memory_32(eventP + 10, event->data.sclExit.pScrollBar ? (uint32_t)((uint8_t *)event->data.sclExit.pScrollBar - ram) : 0);
      m68k_write_memory_16(eventP + 14, event->data.sclExit.value);
      m68k_write_memory_16(eventP + 16, event->data.sclExit.newValue);
      break;
    case sclRepeatEvent:
      m68k_write_memory_16(eventP +  8, event->data.sclRepeat.scrollBarID);
      m68k_write_memory_32(eventP + 10, event->data.sclRepeat.pScrollBar ? (uint32_t)((uint8_t *)event->data.sclRepeat.pScrollBar - ram) : 0);
      m68k_write_memory_16(eventP + 14, event->data.sclRepeat.value);
      m68k_write_memory_16(eventP + 16, event->data.sclRepeat.newValue);
      m68k_write_memory_32(eventP + 18, event->data.sclRepeat.time);
      break;
    case frmGadgetEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.gadgetEnter.gadgetID);
      a = event->data.gadgetEnter.gadgetP ? (uint32_t)((uint8_t *)event->data.gadgetEnter.gadgetP - ram) : 0;
      m68k_write_memory_32(eventP + 10, a);
      encode_gadget(a, event->data.gadgetEnter.gadgetP);
      break;
    case frmGadgetMiscEvent:
      m68k_write_memory_16(eventP +  8, event->data.gadgetMisc.gadgetID);
      a = event->data.gadgetMisc.gadgetP ? (uint32_t)((uint8_t *)event->data.gadgetMisc.gadgetP - ram) : 0;
      m68k_write_memory_32(eventP + 10, a);
      m68k_write_memory_16(eventP + 14, event->data.gadgetMisc.selector);
      m68k_write_memory_32(eventP + 16, event->data.gadgetMisc.dataP ? (uint32_t)((uint8_t *)event->data.gadgetMisc.dataP - ram) : 0);
      encode_gadget(a, event->data.gadgetMisc.gadgetP);
      break;
    case tblEnterEvent:
      m68k_write_memory_16(eventP +  8, event->data.tblEnter.tableID);
      a = event->data.tblEnter.pTable ? (uint32_t)((uint8_t *)event->data.tblEnter.pTable - ram) : 0;
      m68k_write_memory_32(eventP + 10, a);
      m68k_write_memory_16(eventP + 14, event->data.tblEnter.row);
      m68k_write_memory_16(eventP + 16, event->data.tblEnter.column);
      break;
    case tblExitEvent:
      m68k_write_memory_16(eventP +  8, event->data.tblExit.tableID);
      a = event->data.tblExit.pTable ? (uint32_t)((uint8_t *)event->data.tblExit.pTable - ram) : 0;
      m68k_write_memory_32(eventP + 10, a);
      m68k_write_memory_16(eventP + 14, event->data.tblExit.row);
      m68k_write_memory_16(eventP + 16, event->data.tblExit.column);
      break;
    case tblSelectEvent:
      m68k_write_memory_16(eventP +  8, event->data.tblSelect.tableID);
      a = event->data.tblSelect.pTable ? (uint32_t)((uint8_t *)event->data.tblSelect.pTable - ram) : 0;
      m68k_write_memory_32(eventP + 10, a);
      m68k_write_memory_16(eventP + 14, event->data.tblSelect.row);
      m68k_write_memory_16(eventP + 16, event->data.tblSelect.column);
      break;
    case winEnterEvent:
      a = event->data.winEnter.enterWindow ? (uint32_t)((uint8_t *)event->data.winEnter.enterWindow - ram) : 0;
      m68k_write_memory_32(eventP + 8, a);
      a = event->data.winEnter.exitWindow ? (uint32_t)((uint8_t *)event->data.winEnter.exitWindow - ram) : 0;
      m68k_write_memory_32(eventP + 12, a);
      break;
    case winExitEvent:
      a = event->data.winExit.enterWindow ? (uint32_t)((uint8_t *)event->data.winExit.enterWindow - ram) : 0;
      m68k_write_memory_32(eventP + 8, a);
      a = event->data.winExit.exitWindow ? (uint32_t)((uint8_t *)event->data.winExit.exitWindow - ram) : 0;
      m68k_write_memory_32(eventP + 12, a);
      break;
    default:
      if (event->eType < firstUserEvent /*|| event->eType > lastUserEvent*/) {
        sys_snprintf(buf, sizeof(buf)-1, "encode event %s (%d) incomplete", EvtGetEventName(event->eType), event->eType);
        emupalmos_panic(buf, EMUPALMOS_UNDECODED_EVENT);
      } else {
        for (i = 0; i < 16; i++) {
          m68k_write_memory_8(eventP + 8 + i, event->data.buffer[i]);
        }
      }
      break;
  }
}

void decode_rgb(uint32_t rgbP, RGBColorType *rgb) {
  if (rgbP && rgb) {
    rgb->index = m68k_read_memory_8(rgbP);
    rgb->r = m68k_read_memory_8(rgbP+1);
    rgb->g = m68k_read_memory_8(rgbP+2);
    rgb->b = m68k_read_memory_8(rgbP+3);
  }
}

void encode_rgb(uint32_t rgbP, RGBColorType *rgb) {
  if (rgbP && rgb) {
    m68k_write_memory_8(rgbP,     rgb->index);
    m68k_write_memory_8(rgbP + 1, rgb->r);
    m68k_write_memory_8(rgbP + 2, rgb->g);
    m68k_write_memory_8(rgbP + 3, rgb->b);
  }
}

void decode_rectangle(uint32_t rP, RectangleType *rect) {
  if (rP && rect) {
    rect->topLeft.x = m68k_read_memory_16(rP);
    rect->topLeft.y = m68k_read_memory_16(rP + 2);
    rect->extent.x  = m68k_read_memory_16(rP + 4);
    rect->extent.y  = m68k_read_memory_16(rP + 6);
  }
}

void encode_rectangle(uint32_t rP, RectangleType *rect) {
  if (rP && rect) {
    m68k_write_memory_16(rP,     rect->topLeft.x);
    m68k_write_memory_16(rP + 2, rect->topLeft.y);
    m68k_write_memory_16(rP + 4, rect->extent.x);
    m68k_write_memory_16(rP + 6, rect->extent.y);
  }
}

void decode_point(uint32_t pP, PointType *point) {
  if (pP && point) {
    point->x = m68k_read_memory_16(pP);
    point->y = m68k_read_memory_16(pP + 2);
  }
}

void encode_point(uint32_t pP, PointType *point) {
  if (pP && point) {
    m68k_write_memory_16(pP,     point->x);
    m68k_write_memory_16(pP + 2, point->y);
  }
}

void encode_deviceinfo(uint32_t deviceInfoP, DeviceInfoType *deviceInfo) {
  if (deviceInfoP && deviceInfo) {
    uint8_t *ram = pumpkin_heap_base();
    m68k_write_memory_32(deviceInfoP +  0, deviceInfo->serDevCreator);
    m68k_write_memory_32(deviceInfoP +  4, deviceInfo->serDevFtrInfo);
    m68k_write_memory_32(deviceInfoP +  8, deviceInfo->serDevMaxBaudRate);
    m68k_write_memory_32(deviceInfoP + 12, deviceInfo->serDevHandshakeBaud);
    m68k_write_memory_32(deviceInfoP + 16, deviceInfo->serDevPortInfoStr ? ((uint8_t *)deviceInfo->serDevPortInfoStr - ram) : 0);
  }
}

void decode_NetSocketAddrType(uint32_t addrP, NetSocketAddrType *a) {
  if (addrP && a) {
    NetSocketAddrINType *addr = (NetSocketAddrINType *)a;
    addr->family = m68k_read_memory_16(addrP + 0);
    addr->port   = _NetSwap16(m68k_read_memory_16(addrP + 2));
    addr->addr   = m68k_read_memory_32(addrP + 4);
  }
}

void encode_NetSocketAddrType(uint32_t addrP, NetSocketAddrType *a) {
  if (addrP && a) {
    NetSocketAddrINType *addr = (NetSocketAddrINType *)a;
    m68k_write_memory_32(addrP + 0, addr->family);
    m68k_write_memory_32(addrP + 2, _NetSwap16(addr->port));
    m68k_write_memory_32(addrP + 4, addr->addr);
  }
}

/*
typedef struct {
  Char *    nameP;         // official name of host
  Char **   nameAliasesP;  // array of alias's for the name
  UInt16    addrType;      // address type of return addresses
  UInt16    addrLen;       // the length, in bytes, of the addresse
                           // Note this denotes length of a address, not # of addresses.
  UInt8 **  addrListP;     // array of ptrs to addresses in HBO
} NetHostInfoType;

typedef struct {
  NetHostInfoType hostInfo;         // high level results of call are here

  // The following fields contain the variable length data that hostInfo points to
  Char  name[netDNSMaxDomainName+1];      // hostInfo->name
  Char *aliasList[netDNSMaxAliases+1];    // +1 for 0 termination.
  Char  aliases[netDNSMaxAliases][netDNSMaxDomainName+1];

  NetIPAddr *addressList[netDNSMaxAddresses];
  NetIPAddr address[netDNSMaxAddresses];

} NetHostInfoBufType;
*/

void encode_NetHostInfoBufType(uint32_t bufP, NetHostInfoBufType *buf) {
  uint32_t i, j, offset, offset0;

  if (bufP && buf) {
    // hostInfo
    m68k_write_memory_32(bufP +  0, bufP + 16);
    m68k_write_memory_16(bufP +  8, buf->hostInfo.addrType);
    m68k_write_memory_16(bufP + 10, buf->hostInfo.addrLen);

    offset = 16;
    // name
    for (i = 0; i < netDNSMaxDomainName+1; i++, offset++) {
      m68k_write_memory_8(bufP + offset, buf->name[i]);
    }

    m68k_write_memory_32(bufP + 4, bufP + offset);

    // aliases
    offset0 = offset;
    for (i = 0; i < netDNSMaxAliases; i++, offset += 4) {
      m68k_write_memory_32(bufP + offset, bufP + offset0 + (netDNSMaxAliases+1)*4 + i*(netDNSMaxDomainName+1));
    }
    m68k_write_memory_32(bufP + offset, 0);
    offset += 4;
    for (i = 0; i < netDNSMaxAliases; i++) {
      for (j = 0; j < netDNSMaxDomainName+1; j++, offset++) {
        m68k_write_memory_8(bufP + offset, buf->aliases[i][j]);
      }
    }

    m68k_write_memory_32(bufP + 12, bufP + offset);

    // addresses
    offset0 = offset;
    for (i = 0; i < netDNSMaxAddresses; i++, offset += 4) {
      m68k_write_memory_32(bufP + offset, bufP + offset0 + netDNSMaxAddresses*4 + i*4);
    }
    for (i = 0; i < netDNSMaxAddresses; i++, offset += 4) {
      m68k_write_memory_32(bufP + offset, buf->address[i]);
    }
  }
}

/*
typedef struct {
  Char name[netConfigNameSize]; // name of configuration
} NetConfigNameType;
*/

void decode_NetConfigNameType(uint32_t nameArrayP, NetConfigNameType *nameArray) {
  uint32_t i;

  if (nameArrayP && nameArray) {
    for (i = 0; i < netConfigNameSize; i++) {
      nameArray->name[i] = m68k_read_memory_8(nameArrayP + i);
    }
  }
}

void encode_NetConfigNameType(uint32_t nameArrayP, NetConfigNameType *nameArray) {
  uint32_t i;

  if (nameArrayP && nameArray) {
    for (i = 0; i < netConfigNameSize; i++) {
      m68k_write_memory_8(nameArrayP + i, nameArray->name[i]);
    }
  }
}

void decode_FileInfoType(uint32_t fileInfoP, FileInfoType *fileInfo) {
  if (fileInfoP && fileInfo) {
    uint8_t *ram = pumpkin_heap_base();
    uint32_t a = m68k_read_memory_32(fileInfoP + 4);
    fileInfo->nameP = a ? (char *)ram + a : NULL;
    fileInfo->nameBufLen = m68k_read_memory_16(fileInfoP + 8);
  }
}

void encode_FileInfoType(uint32_t fileInfoP, FileInfoType *fileInfo) {
  uint32_t i;

  if (fileInfoP && fileInfo) {
    m68k_write_memory_32(fileInfoP + 0, fileInfo->attributes);
    if (fileInfo->nameP) {
      uint32_t a = m68k_read_memory_32(fileInfoP + 4);
      for (i = 0; i < fileInfo->nameBufLen && fileInfo->nameP[i]; i++) {
        m68k_write_memory_8(a + i, fileInfo->nameP[i]);
      }
      if (i < fileInfo->nameBufLen) {
        m68k_write_memory_8(a + i, 0);
      }
    }
  }
}

void decode_smfoptions(uint32_t selP, SndSmfOptionsType *options) {
  if (selP && options) {
    options->dwStartMilliSec = m68k_read_memory_32(selP);
    options->dwEndMilliSec = m68k_read_memory_32(selP + 4);
    options->amplitude = m68k_read_memory_16(selP + 8);
    options->interruptible = m68k_read_memory_8(selP + 10);
  }
}

void decode_sndcmd(uint32_t cmdP, SndCommandType *cmd) {
  if (cmdP && cmd) {
    cmd->cmd = m68k_read_memory_8(cmdP);
    cmd->param1 = m68k_read_memory_32(cmdP + 2);
    cmd->param2 = m68k_read_memory_16(cmdP + 6);
    cmd->param3 = m68k_read_memory_16(cmdP + 8);
  }
}

/*
typedef struct VolumeInfoTag {
        UInt32  attributes;                     // read-only etc.
        UInt32  fsType;                         // Filesystem type for this volume (defined below)
        UInt32  fsCreator;                      // Creator code of filesystem driver for this volume.  For use with VFSCustomControl().
        UInt32  mountClass;                     // mount class that mounted this volume

        // For slot based filesystems: (mountClass = vfsMountClass_SlotDriver)
        UInt16  slotLibRefNum;          // Library on which the volume is mounted
        UInt16  slotRefNum;                     // ExpMgr slot number of card containing volume
        UInt32  mediaType;                      // Type of card media (mediaMemoryStick, mediaCompactFlash, etc...)
        UInt32  reserved;                       // reserved for future use (other mountclasses may need more space)
} VolumeInfoType;
*/

void encode_VolumeInfoType(uint32_t volInfoP, VolumeInfoType *volInfo) {
  if (volInfoP && volInfo) {
    m68k_write_memory_32(volInfoP +  0, volInfo->attributes);
    m68k_write_memory_32(volInfoP +  4, volInfo->fsType);
    m68k_write_memory_32(volInfoP +  8, volInfo->fsCreator);
    m68k_write_memory_32(volInfoP + 12, volInfo->mountClass);
    m68k_write_memory_16(volInfoP + 16, volInfo->slotLibRefNum);
    m68k_write_memory_16(volInfoP + 18, volInfo->slotRefNum);
    m68k_write_memory_32(volInfoP + 20, volInfo->mediaType);
    m68k_write_memory_32(volInfoP + 24, volInfo->reserved);
  }
}

void decode_notify(uint32_t notifyP, SysNotifyParamType *notify) {
  char buf1[8], buf2[8];
  notif_union_t param;

  if (notifyP && notify) {
    uint8_t *ram = pumpkin_heap_base();
    notify->notifyType = m68k_read_memory_32(notifyP + 0);
    notify->broadcaster = m68k_read_memory_32(notifyP + 4);
    uint32_t notifyDetailsP = m68k_read_memory_32(notifyP + 8);
    uint32_t userDataP = m68k_read_memory_32(notifyP + 12);
    notify->userDataP = userDataP ? ram + userDataP : NULL;
    notify->handled = m68k_read_memory_8(notifyP + 16);

    pumpkin_id2s(notify->notifyType, buf1);
    if (notify->broadcaster) {
      pumpkin_id2s(notify->broadcaster, buf2);
    } else {
      sys_strcpy(buf2, "<none>");
    }
    debug(DEBUG_TRACE, "EmuPalmOS", "SysNotifyParamType '%s' from '%s' details 0x%08X handled %d", buf1, buf2, notifyDetailsP, notify->handled);

    if (notifyDetailsP) {
      notify->notifyDetailsP = &param;
      decode_notif(notify->notifyType, notifyDetailsP, (notif_union_t *)notify->notifyDetailsP);
    }
  }
}

void encode_notify(uint32_t notifyP, SysNotifyParamType *notify) {
  if (notifyP && notify) {
    uint8_t *ram = pumpkin_heap_base();
    uint32_t notifyDetailsP, userDataP;

    if (notify->notifyDetailsP) {
      notifyDetailsP = notifyP + 18; // after SysNotifyParamType
      encode_notif(notify->notifyType, notifyDetailsP, (notif_union_t *)notify->notifyDetailsP);
    } else {
      notifyDetailsP = 0;
    }

    userDataP = notify->userDataP ? ((uint8_t *)notify->userDataP - ram) : 0;

    m68k_write_memory_32(notifyP +  0, notify->notifyType);
    m68k_write_memory_32(notifyP +  4, notify->broadcaster);
    m68k_write_memory_32(notifyP +  8, notifyDetailsP);
    m68k_write_memory_32(notifyP + 12, userDataP);
    m68k_write_memory_8(notifyP +  16, notify->handled);
    m68k_write_memory_8(notifyP +  17, 0); // reserved2
  }
}

static int cpu_instr_callback(unsigned int pc);
static int cpu_instr_callback2(unsigned int pc);

// unsigned long Call68KFuncType(const void *emulStateP, unsigned long trapOrFunction, const void *argsOnStackP, unsigned long argsSizeAndwantA0)
