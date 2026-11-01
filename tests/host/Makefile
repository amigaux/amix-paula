# tests/host/Makefile -- host tests of /dev/paula: the driver compiled
# with the host's cc against a fake of the hardware and the kernel
# (fake.c), once as the static driver and once as the module.  GNU make.
#
#   make -C tests/host          build and run both
#   make -C tests/host clean
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.

TOP    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
HERE   := $(TOP)/tests/host
OUT    := $(TOP)/build/tests
CC     ?= cc
CFLAGS  = -std=gnu89 -g -O1 -Wall -Wno-unused-function -Wno-implicit-int \
	-Wno-old-style-definition -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-builtin-declaration-mismatch -Wno-deprecated-non-prototype -Wno-int-to-pointer-cast \
	-fno-builtin -iquote $(HERE)/stub -iquote $(TOP)/port/src -iquote $(TOP)/module -I$(HERE)
SRC     = $(TOP)/port/src/paula.c $(TOP)/port/src/paula.h $(TOP)/module/paula_mod.c \
	$(wildcard $(HERE)/*.[ch] $(HERE)/stub/*.h $(HERE)/stub/sys/*.h)

all: test

$(OUT)/t-static: $(SRC)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(HERE)/test.c $(HERE)/fake.c $(HERE)/drv_static.c

$(OUT)/t-module: $(SRC)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(HERE)/test.c $(HERE)/fake.c $(HERE)/drv_mod.c

test: $(OUT)/t-static $(OUT)/t-module
	$(OUT)/t-static
	$(OUT)/t-module

clean:
	rm -rf "$(OUT)"

.PHONY: all test clean
