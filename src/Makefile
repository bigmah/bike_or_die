# Builds the statically recompiled Bike or Die 2 code into a static library
# that libpumpkin links against.
ROOT   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))/..
CC     ?= clang
OPT    ?= -O1
GUARD  ?= -DRECOMP_GUARD
CFLAGS := $(OPT) -fPIC $(GUARD) -Wall -Wno-unused-label -Wno-unused-variable \
          -Wno-unused-but-set-variable -I$(ROOT)/src/recomp -I$(ROOT)/src/gen
# The single-step cores (gen/arm<n>_step.c) are only needed by BOD_ARM_LOCKSTEP
# and are generated on demand, so build whichever ones are present.
STEPPERS := $(patsubst $(ROOT)/src/%,%,$(wildcard $(ROOT)/src/gen/arm*_step.c))
SRCS   := gen/seg1.c gen/seg2.c gen/seg3.c gen/r68k_tables.c recomp/r68k_run.c \
          gen/arm1.c gen/arm2.c gen/arm3.c gen/arm4.c gen/rarm_tables.c \
          gen/rarm_steppers.c $(STEPPERS) recomp/rarm_run.c
OBJS   := $(patsubst %.c,$(ROOT)/build/obj/%.o,$(SRCS))
LIB    := $(ROOT)/build/librecomp.a

all: $(LIB)

$(LIB): $(OBJS)
	@echo "Creating $@"
	@ar -cr $@ $(OBJS)

$(ROOT)/build/obj/%.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@rm -f $(OBJS) $(LIB)

.PHONY: all clean
