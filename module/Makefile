# module/Makefile -- /dev/paula as a loadable module, one per kernel line, built
# with the amix module kit (kernel/dlm; its docs/guide.md, contract C3-6).
# GNU make.
#
#   make -C module            both lines: build/k30/mod.d/paula and
#                             build/k46/mod.d/paula, each checked by the kit's
#                             modfix and modcheck.py, and by `srcheck` below
#   make -C module k46        one line (or k30)
#   make -C module KIT=<kit>  one line, that kit's
#   make -C module clean
#
# DLM is kernel/dlm of an amix checkout (github.com/amigaux/amix) whose kits
# are built (`make kit` there gives build/kit/k30 and build/kit/k46); the
# default is ../amix/kernel/dlm beside this repo.  KIT_k30 / KIT_k46 name a
# kit directly.
# The pinned cross tools (m68k-cbm-sysv4-*, gcc-cross-amix) and python3 must
# be on PATH.  The source is port/src/paula.c, the static driver's own file,
# included by module/paula_mod.c with PAULA_MODULE defined.
#
# Each line builds in its staging directory, build/<line>/stage: the kit's
# make fragment runs there, on copies of paula_mod.c, paula.c and paula.h,
# the kit's Amiga memory.h (so `#include "memory.h"` cannot take libc's
# <memory.h>, as in the static relink pass) and the Master it generates
# from keys.  Master has no $cpu, so the kit gives each line its own; the
# cdevsw major is the one in keys.  Nothing is written outside build/.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
DLM ?= $(abspath $(TOP)/../amix/kernel/dlm)
LINES = k30 k46
kit = $(or $(KIT_$(1)),$(DLM)/build/kit/$(1))
kitline = $(shell sed -n 's/^LINE=//p' $(1)/kit.conf)

ifeq ($(KIT),)

all: $(LINES)

$(LINES):
	$(MAKE) -f $(TOP)/module/Makefile KIT=$(call kit,$@)

clean: $(LINES:%=clean-%)

$(LINES:%=clean-%): clean-%:
	$(MAKE) -f $(TOP)/module/Makefile KIT=$(call kit,$*) clean

.PHONY: all clean $(LINES) $(LINES:%=clean-%)

else ifeq ($(IN_STAGE),)	# one line: into its stage

STAGE := $(TOP)/build/$(call kitline,$(KIT))/stage

all clean Master srcheck:
	@mkdir -p $(STAGE)
	$(MAKE) -C $(STAGE) -f $(TOP)/module/Makefile KIT=$(KIT) IN_STAGE=1 $@

.PHONY: all clean Master srcheck

else	# one line, the kit's, in its stage

MODULE = paula
OBJS   = paula_mod.o
MODOUT = $(TOP)/build/$(LINE)
MAJOR := $(shell sed -n 's/^majors=c:\([0-9]*\)$$/\1/p' $(TOP)/module/keys)
CFLAGS = -DPAULA_MAJOR=$(MAJOR) -Dsigned=__signed__
include $(KIT)/mk/module.mk
OBJDUMP = $(KIT_CC:%-gcc=%-objdump)

all: srcheck

# the staged sources: the kit's %.o rule compiles paula_mod.c from here
paula_mod.c: $(TOP)/module/paula_mod.c
	cp $< $@
paula.c paula.h: %: $(TOP)/port/src/%
	cp $< $@
# the Amiga memory.h (AllocMem, MEMF_CHIP), not libc's that the -I order finds first
memory.h: $(KIT)/stock/usr/sys/amiga/inc/memory.h
	cp $< $@
$(MODOUT)/obj/paula_mod.o: paula.c paula.h memory.h $(TOP)/module/keys

Master: $(TOP)/module/keys
	{ echo '# generated from module/keys by module/Makefile'; \
	  echo '$$modtype drv'; \
	  sed -n 's/^\([a-z]*\)=\(.*\)$$/$$\1 \2/p' $< | grep -v '^\$$name '; \
	  echo '$(MODULE) paula'; } > $@

# -traditional may drop an asm it thinks dead (spl4/splx): the object must
# still write SR, as the static relink pass checks for paula_040.o.  The
# interface header has two copies (include/ for programs, port/src/ for
# the static relink pass); they must be the same bytes.
srcheck: $(MODOUT)/mod.d/$(MODULE)
	@cmp -s $(TOP)/include/paula.h $(TOP)/port/src/paula.h || \
		{ echo "paula: include/paula.h and port/src/paula.h differ" >&2; exit 1; }
	@n=$$($(OBJDUMP) -d $(MODOUT)/obj/paula_mod.o | grep -cE ',%sr$$'); \
	[ "$$n" -gt 0 ] || { echo "paula: no SR write in paula_mod.o: spl4/splx lost" >&2; exit 1; }; \
	echo "paula: $$n SR writes (spl4/splx present)"; \
	cd $(TOP)/build && sha256sum $(LINE)/mod.d/$(MODULE)

.PHONY: srcheck

endif
