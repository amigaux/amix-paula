# tests/Makefile -- the host tests (GNU make):
#
#   make -C tests            all of them
#   make -C tests host       the driver on a fake of the hardware (host cc)
#   make -C tests cdevsw     the static form's cdevsw patcher (cross as/ld)
#   make -C tests install    install.sh
#
# KERNEL=<a relocatable static k46 kernel> runs the cdevsw test on it too.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.

HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

all: host cdevsw install

host:
	$(MAKE) -C $(HERE)/host

cdevsw:
	sh $(HERE)/cdevsw/test.sh

install:
	sh $(HERE)/install/test.sh

.PHONY: all host cdevsw install
