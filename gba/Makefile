# Bike or Die 2 for the Game Boy Advance.
#   make            -> ../build/gba/bod.gba
#
# Three groups of sources go into the ROM: the statically recompiled 68k code
# (src/gen), the PumpkinOS Palm OS implementation compiled in place from the
# vendor tree plus the trap marshalling copied by tools/gba/sync_pumpkin.sh,
# and this directory's own platform layer.
PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
GBA     := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT    := $(GBA)/..
PK      := $(ROOT)/vendor/PumpkinOS/src
BUILD   := $(ROOT)/build/gba
TARGET  := $(BUILD)/bod

ARCH    := -mcpu=arm7tdmi -mthumb-interwork
INCS    := -iquote $(GBA)/shim -I$(GBA)/shim -I$(GBA)/src -I$(ROOT)/src/gen \
           -I$(PK)/libpumpkin -I$(PK)/libpumpkin/emulation -I$(PK)/libpit -I$(PK)/font \
           -I$(PK)/PalmOS -I$(PK)/PalmOS/Core -I$(PK)/PalmOS/Core/System -I$(PK)/PalmOS/Core/UI \
           -I$(PK)/PalmOS/Core/Hardware -I$(PK)/PalmOS/Libraries -I$(PK)/PalmOS/Dynamic \
           -I$(PK)/PalmOS/Extensions/ExpansionMgr -I$(PK)/PalmOS/PalmOne/Common/System -I$(PK)/PalmOS/PalmOne/Common/Libraries -I$(PK)/PalmOS/Libraries/PalmOSGlue -I$(PK)/PalmOS/Libraries/CPMLib -I$(PK)/PalmOS/Libraries/INet -I$(PK)/PalmOS/Libraries/Pdi -I$(PK)/PalmOS/Libraries/SSL -I$(PK)/PalmOS/Extensions/Bluetooth -I$(PK)/PalmOS/Sony -I$(PK)/PalmOS/Sony/System -I$(PK)/PalmOS/Sony/Libraries -I$(PK)/PalmOS/PalmOne -I$(PK)/PalmOS/PalmOne/68K/System -I$(PK)/PalmOS/Garmin \
           -I$(GBA)/pk/emulation
DEFS    := -DSYS_CPU=1 -DSYS_SIZE=32 -DSYS_OS=1 -DSYS_ENDIAN=1 -DBOD_GBA=1 -DARMEMU=1
ifneq ($(TRACE),)
DEFS    += -DBOD_TRACE="\"$(TRACE)\""
endif
COMMON  := $(ARCH) -mthumb -ffreestanding -fno-builtin -fsigned-char -Wno-multichar \
           -fomit-frame-pointer -ffunction-sections -fdata-sections \
           -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables $(INCS) $(DEFS)
CFLAGS  := $(COMMON) -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
PKFLAGS := $(COMMON) -Os -w
GENFLAGS:= $(COMMON) -O1 -w
ASFLAGS := $(ARCH)
LDFLAGS := $(ARCH) -mthumb -nostdlib -T $(GBA)/gba.ld -Wl,--gc-sections -Wl,-Map,$(TARGET).map
LIBS    := -lgcc

# PumpkinOS: the Palm OS UI and utility managers, compiled in place.
PK_SRCS := Rect Font Window Bitmap Form Control List Field Menu Event UIColor ScrollBar \
           Progress KeyMgr StringMgr DateTime SysUtils Preferences FeatureMgr InsPoint Table \
           TextMgr CharAttr TimeMgr Crc ColorTable fill UIResources UIControls \
           ErrorBase FatalAlert notif_serde launch_serde SystemMgr Category Clipboard PenInputMgr
# PumpkinOS: 68k trap marshalling (copies in pk/emulation).
TRAP_SRCS := $(wildcard $(GBA)/pk/emulation/*.c)
# libpit helpers.
PIT_SRCS := rgb timeutc bytes
# The recompiled 68k code.
GEN_SRCS := seg1 seg2 seg3 r68k_tables

OBJS := $(patsubst %.c,$(BUILD)/obj/src/%.o,$(notdir $(wildcard $(GBA)/src/*.c))) \
        $(patsubst %.s,$(BUILD)/obj/src/%.o,$(notdir $(wildcard $(GBA)/src/*.s))) \
        $(BUILD)/obj/src/crt0.o \
        $(patsubst %,$(BUILD)/obj/pk/%.o,$(PK_SRCS)) \
        $(patsubst %,$(BUILD)/obj/pit/%.o,$(PIT_SRCS)) \
        $(patsubst %.c,$(BUILD)/obj/trap/%.o,$(notdir $(filter-out %/sc_case.c,$(TRAP_SRCS)))) \
        $(patsubst %,$(BUILD)/obj/gen/%.o,$(GEN_SRCS))

all: $(TARGET).gba

$(TARGET).gba: $(TARGET).elf
	@$(OBJCOPY) -O binary $< $@
	@python3 $(GBA)/fixrom.py $@

$(TARGET).elf: $(OBJS) $(GBA)/gba.ld $(BUILD)/data.bin
	@echo "Linking $@"
	@$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@$(PREFIX)size $@

$(BUILD)/obj/src/%.o: $(GBA)/src/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<
$(BUILD)/obj/src/%.o: $(GBA)/src/%.s
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -x assembler-with-cpp -c -o $@ $<
$(BUILD)/obj/src/data.o: $(BUILD)/data.bin

# The data image: the game, PumpkinOS's system resources, the level packs.
LEVELPACKS := $(ROOT)/build/levelpacks
$(BUILD)/data.bin: $(ROOT)/tools/gba/packdata.py $(ROOT)/tools/gba/armpatch.py $(ROOT)/tools/gba/hotmove.py $(ROOT)/tools/gba/pretex.py $(wildcard $(ROOT)/tools/gba/patches/*.s) $(ROOT)/files/BikeOrDie-2.1b.prc
	@mkdir -p $(BUILD)
	@python3 $(ROOT)/tools/gba/packdata.py --base 0x08400000 -o $@ --patches $(ROOT)/tools/gba/patches --pretex $(BUILD)/pretex prc:$(ROOT)/files/BikeOrDie-2.1b.prc pkdir:$(PK)/../vfs/app_storage/BOOT_424F4F54 prc:$(ROOT)/files/robot-biker.prc prcdir:$(LEVELPACKS)

$(BUILD)/obj/src/crt0.o: $(GBA)/crt0.s
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -x assembler-with-cpp -c -o $@ $<
$(BUILD)/obj/pk/%.o: $(PK)/libpumpkin/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling pumpkin $(notdir $<)"
	@$(CC) $(PKFLAGS) -MMD -MP -c -o $@ $<
$(BUILD)/obj/pit/%.o: $(PK)/libpit/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling pit $(notdir $<)"
	@$(CC) $(PKFLAGS) -MMD -MP -c -o $@ $<
$(BUILD)/obj/trap/%.o: $(GBA)/pk/emulation/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling trap $(notdir $<)"
	@$(CC) $(PKFLAGS) -MMD -MP -c -o $@ $<
$(BUILD)/obj/gen/%.o: $(ROOT)/src/gen/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling 68k $(notdir $<)"
	@$(CC) $(GENFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)/obj $(TARGET).elf $(TARGET).gba $(TARGET).map

-include $(OBJS:.o=.d)
.PHONY: all clean
