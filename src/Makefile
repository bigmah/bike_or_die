# Builds the statically recompiled Bike or Die 2 code into a static library
# that libpumpkin links against.
# WASM=1 builds the same sources with Emscripten instead, into a separate
# archive and object tree so the two can coexist.
ROOT   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))/..
ifeq ($(WASM),1)
# Plain `?=` would lose to make's built-in CC/AR defaults, which are defined
# rather than undefined, and the wasm build would quietly come out native.
CC     := emcc
AR     := emar
SUFFIX := -wasm
else
CC     ?= clang
AR     := ar
SUFFIX :=
endif
OPT    ?= -O1
GUARD  ?= -DRECOMP_GUARD
CFLAGS := $(OPT) -fPIC $(GUARD) -Wall -Wno-unused-label -Wno-unused-variable \
          -Wno-unused-but-set-variable -I$(ROOT)/src/recomp -I$(ROOT)/src/gen
# The single-step cores (gen/arm<n>_step.c) are only needed by BOD_ARM_LOCKSTEP
# and are generated on demand, so build whichever ones are present.
ifeq ($(WASM),1)
STEPPERS :=
STEPTAB  := recomp/rarm_nosteppers.c
else
STEPPERS := $(patsubst $(ROOT)/src/%,%,$(wildcard $(ROOT)/src/gen/arm*_step.c))
STEPTAB  := gen/rarm_steppers.c
endif
SRCS   := gen/seg1.c gen/seg2.c gen/seg3.c gen/r68k_tables.c recomp/r68k_run.c \
          gen/arm1.c gen/arm2.c gen/arm3.c gen/arm4.c gen/rarm_tables.c \
          $(STEPTAB) $(STEPPERS) recomp/rarm_run.c
OBJS   := $(patsubst %.c,$(ROOT)/build/obj$(SUFFIX)/%.o,$(SRCS))
LIB    := $(ROOT)/build/librecomp$(SUFFIX).a

all: $(LIB)

$(LIB): $(OBJS)
	@echo "Creating $@"
	@$(AR) -cr $@ $(OBJS)

$(ROOT)/build/obj$(SUFFIX)/%.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@rm -f $(OBJS) $(LIB)

.PHONY: all clean
