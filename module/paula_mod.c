/*
 * paula_mod.c -- /dev/paula as a loadable module for the Amix module system
 * (amix-dlm, contract C3-6), one object per kernel line (k30, k46), built by
 * module/Makefile with that line's ABI kit.
 *
 * The driver is port/src/paula.c, the same file the static relink path
 * compiles; it is included here with PAULA_MODULE defined, so a fix made
 * there serves both forms.  What the module form changes:
 *
 *  - Interrupt.  A module may not replace aciaaintr, the level-2 CIA-A
 *    handler; it attaches to the level-2 chain (intr_attach), which runs
 *    after the stock handlers.  The stock aciaaintr has read and so cleared
 *    CIA-A's ICR by then, timer B's bit with it, so the module cannot see
 *    the bit.  It runs timer B one-shot instead: an underflow stops the
 *    timer and clears CRB's START bit, which stays readable.  paula_int2
 *    restarts the timer for the next period, then applies one tick.  Every
 *    level-2 interrupt (the clock's included) calls it, so a stopped timer
 *    is seen at the next clock tick at the latest.  Each period is longer
 *    than a continuous timer's by the time from the underflow to the
 *    restart (the interrupt latency).
 *  - Open and close.  paula_mopen attaches the handler before the timer
 *    starts, then waits for the first tick (the self-test of C3-6 section
 *    1.3); paula_mclose detaches it after the timer stops.
 *  - The cdevsw row and major come from .moddata (module/keys); no patch.
 *
 * Kernel names it needs that DDI v1.2 lacks (module/README.md): AllocMem,
 * FreeMem, displaytype, and the /dev/audio exclusion's paula_busy and
 * audio_nopen.
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.  The driver
 * it includes, port/src/paula.c, is isoriano1968's with our changes (see
 * there).
 */
#define PAULA_MODULE
#include "paula.c"

#include "sys/conf.h"
#include "sys/moddefs.h"

#ifndef PAULA_MAJOR
#define PAULA_MAJOR	54
#endif
#define SELFTEST_TICKS	25	/* clock ticks to wait for the first timer B tick */

extern int intr_attach(), intr_detach();
extern int timeout(), sleep();
extern void wakeup();
extern int printf();

static int attached;		/* paula_int2 is on the level-2 chain */
static int selftest_chan;	/* the self-test's wait channel */

/*
 * On the level-2 chain: one call per level-2 interrupt, whatever raised it.
 * Claims only when timer B ran out: START clear while the driver is active.
 */
static int
paula_int2(arg)
char *arg;
{
	if (!active || (ACIAA->crb & CRB_START))
		return 0;
	/* the next period first: the latch, then a forced load and start */
	ACIAA->tblo = tb_latch & 0xff;
	ACIAA->tbhi = tb_latch >> 8;
	ACIAA->crb = CRB_RUN;
	paula_tick();
	return 1;
}

static void
paula_detach()
{
	if (attached) {
		intr_detach(2, paula_int2, (char *)0);
		attached = 0;
	}
}

int
paula_mopen(dp, oflag, otyp, crp)
dev_t *dp;
int oflag, otyp;
struct cred *crp;
{
	int e, i, s;

	/*
	 * No test of timer B's START: neither kernel line uses timer B, and
	 * Kickstart's timer.device leaves it running (continuous, latch
	 * 0xffff), so START is set at every first open.  paula_start stops it.
	 * The claim comes first: a second opener gets EBUSY, not
	 * intr_attach's EEXIST.
	 */
	if ((e = paula_claim()) != 0)
		return e;
	if ((e = intr_attach(2, paula_int2, (char *)0, "paula")) != 0) {
		paula_release();
		return e;
	}
	attached = 1;
	if ((e = paula_start()) != 0) {
		paula_detach();
		paula_release();
		return e;
	}
	/*
	 * The self-test: the first underflow (20 ms at 125 BPM) reaches us.
	 * One-tick rounds at spl4, so the clock cannot run the timeout
	 * between timeout() and sleep() and lose the wakeup; sleep() lets
	 * the level fall while it waits.  At most SELFTEST_TICKS rounds.
	 */
	s = spl4();
	for (i = 0; i < SELFTEST_TICKS && ticks == 0; i++) {
		timeout(wakeup, (caddr_t)&selftest_chan, 1);
		sleep((caddr_t)&selftest_chan, PZERO);
	}
	splx(s);
	if (ticks == 0) {
		paulaclose(*dp, oflag, otyp, crp);
		paula_detach();
		printf("paula: no timer B interrupt on the level-2 chain\n");
		return EIO;
	}
	return 0;
}

int
paula_mclose(d, oflag, otyp, crp)
dev_t d;
int oflag, otyp;
struct cred *crp;
{
	paulaclose(d, oflag, otyp, crp);
	paula_detach();
	return 0;
}

/*
 * The value of name in the parameter string ("name=value ...", decimal),
 * or def when it is absent or not a number; clamped to lo..hi.
 */
static long
param(params, name, def, lo, hi)
char *params, *name;
long def, lo, hi;
{
	char *p = params, *n;
	long v;

	while (p && *p) {
		while (*p == ' ')
			p++;
		for (n = name; *n && *p == *n; n++, p++)
			;
		if (!*n && *p == '=' && p[1] >= '0' && p[1] <= '9') {
			for (v = 0, p++; *p >= '0' && *p <= '9'; p++)
				v = v * 10 + (*p - '0');
			if (*p == ' ' || !*p)
				return v < lo ? lo : v > hi ? hi : v;
		}
		while (*p && *p != ' ')
			p++;
	}
	return def;
}

/*
 * The parameters (keys: their defaults and limits): clock (0 auto, 1 NTSC,
 * 2 PAL; pick_clock in paula.c), maxarea (SETUP's limit in bytes) and
 * ring (tick slots; PAULA_PUSH queues one fewer).  Nothing else until the
 * first open.
 */
static int
paula_load(params)
char *params;
{
	clock_set = (int)param(params, "clock", 0L, 0L, 2L);
	maxarea = (unsigned long)param(params, "maxarea", PAULA_MAXAREA, 4096L, PAULA_MAXAREA);
	nring = (int)param(params, "ring", (long)PAULA_RING, 4L, (long)PAULA_RING);
	return 0;
}

/* not while open (the core holds the module); also after a failed load */
static int
paula_unload()
{
	if (busy)
		return EBUSY;
	paula_detach();		/* close has stopped the timer already */
	return 0;
}

/* the halt pass: no sleep, interrupts perhaps off; timer B off, the
   channels silent, the LED and filter as the open found them */
static void
paula_halt()
{
	int c;

	if (!busy)		/* timer B and the channels are not ours */
		return;
	ACIAA->icr = ICR_CLR | ICR_TB;
	ACIAA->crb = 0;
	active = 0;
	AMIGA->dmacon = DMACLR | DMA_AUDIO;
	for (c = 0; c < 4; c++)
		AMIGA->audio[c].loud = 0;
	restore_led();
}

struct mod_drv_data paula_drvdata[] = {
	{ { 0 }, 0, 0,
	  { paula_mopen, paula_mclose, 0, 0, paulaioctl },
	  PAULA_MAJOR, 1 }
};

MOD_DRV_WRAPPER(paula, paula_load, paula_unload, paula_halt, "Paula audio channels /dev/paula");
