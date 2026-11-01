# tests/Makefile -- the host tests (GNU make):
#
#   make -C tests            all of them
#   make -C tests host       the driver on a fake of the hardware (host cc)
#   make -C tests cdevsw     the static form's cdevsw patcher (cross as/ld)
#
# KERNEL=<a relocatable static k46 kernel> runs the cdevsw test on it too.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.

HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

all: host cdevsw

host:
	$(MAKE) -C $(HERE)/host

cdevsw:
	sh $(HERE)/cdevsw/test.sh

.PHONY: all host cdevsw
