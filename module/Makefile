# module/Makefile -- /dev/paula as a loadable module, one per kernel line, built
# with amix-dlm's ABI kit (the kit's docs/guide.md; contract C3-6).  GNU make.
#
#   make -C module            both lines: build/k30/mod.d/paula and
#                             build/k46/mod.d/paula, each checked by the kit's
#                             modfix and modcheck.py, and by `srcheck` below
#   make -C module k46        one line (or k30)
#   make -C module clean
#
# DLM is the amix-dlm checkout whose kits are built (`make kit` there gives
# build/kit/k30 and build/kit/k46); KIT_k30 / KIT_k46 name a kit directly.
# The pinned cross tools (m68k-cbm-sysv4-*, gcc-cross-amix) and python3 must
# be on PATH.  The source is port/src/paula.c, the static driver's own file,
# included by module/paula_mod.c with PAULA_MODULE defined.  Like the static
# relink pass, the build compiles from a staging directory holding paula.c,
# paula.h and the kit's Amiga memory.h, so `#include "memory.h"` cannot take
# libc's <memory.h>.  Master is generated from keys; it has no $cpu, so the
# kit gives each line its own.  The cdevsw major is the one in keys.

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
DLM ?= $(abspath $(TOP)/../amix-dlm)
LINES = k30 k46
kit = $(or $(KIT_$(1)),$(DLM)/build/kit/$(1))

ifeq ($(KIT),)

all: $(LINES)

$(LINES):
	$(MAKE) KIT=$(call kit,$@)

clean: $(LINES:%=clean-%)

$(LINES:%=clean-%): clean-%:
	$(MAKE) KIT=$(call kit,$*) clean

.PHONY: all clean $(LINES) $(LINES:%=clean-%)

else	# one line, the kit's

MODULE = paula
OBJS   = paula_mod.o
MODOUT = $(TOP)/build/$(LINE)
STAGE  = $(MODOUT)/stage
MAJOR := $(shell sed -n 's/^majors=c:\([0-9]*\)$$/\1/p' $(TOP)/module/keys)
CFLAGS = -DPAULA_MAJOR=$(MAJOR) -Dsigned=__signed__
include $(KIT)/mk/module.mk

all: srcheck

STAGED = $(STAGE)/paula_mod.c $(STAGE)/paula.c $(STAGE)/paula.h $(STAGE)/memory.h

$(STAGE)/paula_mod.c: $(TOP)/module/paula_mod.c
	@mkdir -p $(STAGE) && cp $< $@
$(STAGE)/paula.c $(STAGE)/paula.h: $(STAGE)/%: $(TOP)/port/src/%
	@mkdir -p $(STAGE) && cp $< $@
# the Amiga memory.h (AllocMem, MEMF_CHIP), not libc's that the -I order finds first
$(STAGE)/memory.h: $(KIT)/stock/usr/sys/amiga/inc/memory.h
	@mkdir -p $(STAGE) && cp $< $@

# the kit's pattern rule, from the staging directory
$(MODOUT)/obj/paula_mod.o: $(STAGED) $(KIT)/kit.conf $(TOP)/module/keys
	@mkdir -p $(dir $@)
	cd $(STAGE) && $(KIT_CC) $(KIT_CFLAGS) $(CFLAGS) -c paula_mod.c -o $@

# -traditional may drop an asm it thinks dead (spl4/splx): the object must
# still write SR, as the static relink pass checks for paula_040.o
srcheck: $(MODOUT)/mod.d/$(MODULE)
	@n=$$(m68k-cbm-sysv4-objdump -d $(MODOUT)/obj/paula_mod.o | grep -cE ',%sr$$'); \
	[ "$$n" -gt 0 ] || { echo "paula: no SR write in paula_mod.o: spl4/splx lost" >&2; exit 1; }; \
	echo "paula: $$n SR writes (spl4/splx present)"; \
	cd $(TOP)/build && sha256sum $(LINE)/mod.d/$(MODULE)

Master: keys
	{ echo '# generated from keys by module/Makefile'; \
	  echo '$$modtype drv'; \
	  sed -n 's/^\([a-z]*\)=\(.*\)$$/$$\1 \2/p' keys | grep -v '^\$$name '; \
	  echo '$(MODULE) paula'; } > Master

.PHONY: srcheck

endif
