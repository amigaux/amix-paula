/*
 * test.c -- host tests of /dev/paula, built twice: against the static
 * driver (drv_static.c) and against the module (drv_mod.c).  The hardware
 * and the kernel are fake.c's.
 *
 * The driver takes user addresses in an int (the ioctl argument), so the
 * "user" buffers live in the low 2 GB (MAP_32BIT).
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "sys/types.h"
#include "amigahr.h"
#include "paula.h"
#include "fake.h"
#include "drv.h"

static int fails, checks;
static const char *cur;

#define CHECK(c) do { checks++; if (!(c)) { fails++; \
	fprintf(stdout, "FAIL %s: %s:%d: %s\n", cur, __FILE__, __LINE__, #c); } } while (0)

static char *user;		/* low memory for "user" buffers */

static struct paula_tick *
utick(void)
{
	struct paula_tick *t = (struct paula_tick *)user;

	memset(t, 0, sizeof *t);
	return t;
}

static void
op(struct paula_tick *t, int o, int c, int v, unsigned long off)
{
	struct paula_op *p = &t->op[t->nops++];

	p->op = o;
	p->ch = c;
	p->val = v;
	p->off = off;
}

static void
timer_on_tick(void)
{
	if (fake_acia.crb & 1)	/* START */
		drv_timer();
}

static void
start(const char *name)
{
	cur = name;
	fake_reset();
	fake_on_tick = timer_on_tick;
}

static int
setup(unsigned long n)
{
	struct paula_load *ld = (struct paula_load *)(user + 4096);
	int i;

	if (drv_ioctl(PAULA_SETUP, (long)n))
		return -1;
	for (i = 0; i < 64; i++)
		user[8192 + i] = (char)(i * 4);
	ld->off = 0;
	ld->len = 64;
	ld->data = user + 8192;
	return drv_ioctl(PAULA_LOAD, (long)ld);
}

static void
t_open_close(void)
{
	start("open_close");
	CHECK(drv_open() == 0);
	CHECK(drv_busy() && paula_busy);
	CHECK(fake_hung == 0);
	CHECK(drv_close() == 0);
	CHECK(!drv_busy() && !paula_busy);
	CHECK(fake_kmem_live == 0 && fake_chip_live == 0);
	CHECK(drv_open() == 0);		/* and again */
	CHECK(drv_close() == 0);
	if (drv_module())
		CHECK(fake_int2 == 0);
}

static void
t_exclusion(void)
{
	start("exclusion");
	audio_nopen = 1;
	CHECK(drv_open() == EBUSY);
	CHECK(!drv_busy() && !paula_busy);
	audio_nopen = 0;
	CHECK(drv_open() == 0);
	CHECK(drv_open() == EBUSY);	/* one holder */
	CHECK(drv_close() == 0);
}

/* PAL or NTSC from the Agnus/Alice id; displaytype reported */
static unsigned
latch(void)
{
	return fake_acia.tbhi << 8 | fake_acia.tblo;
}

static void
clock_case(const char *name, unsigned vposr, unsigned dt, unsigned lt,
    unsigned long flags, unsigned long clock)
{
	struct paula_info *in = (struct paula_info *)(user + 12288);

	start(name);
	fake_amiga.vposr = vposr;
	displaytype = dt;
	CHECK(drv_open() == 0);
	CHECK(latch() == lt);			/* 125 BPM */
	memset(in, 0xff, sizeof *in);
	CHECK(drv_ioctl(PAULA_INFO, (long)in) == 0);
	CHECK(in->version == PAULA_VERSION);
	CHECK(in->flags == flags);
	CHECK(in->clock == clock && in->eclock == clock / 5);
	CHECK(in->queue == PAULA_RING - 1 && in->maxops == PAULA_MAXOPS);
	CHECK(in->maxarea == PAULA_MAXAREA);
	CHECK(drv_close() == 0);
}

static void
t_clock(void)
{
	clock_case("clock_pal", 0x2000, 2, 14187, 0, 3546895);		/* ECS PAL */
	clock_case("clock_ntsc", 0x3000, 1, 14318,
	    PI_NTSC | PI_IDNTSC | PI_FIELD60, 3579545);			/* ECS NTSC */
	clock_case("clock_aga_ntsc", 0x3200, 1, 14318,
	    PI_NTSC | PI_IDNTSC | PI_FIELD60, 3579545);			/* Alice NTSC */
	/* a PAL machine whose beam was set to 60 Hz fields: still PAL */
	clock_case("clock_pal_60hz", 0x2200, 1, 14187, PI_FIELD60, 3546895);
}

/* the ring: KM_NOSLEEP, and a clean ENOMEM */
static void
t_nosleep(void)
{
	start("nosleep");
	CHECK(drv_open() == 0);
	CHECK(fake_kmem_flag == 1);		/* KM_NOSLEEP */
	CHECK(drv_close() == 0);
	fake_kmem_fail = 1;
	CHECK(drv_open() == ENOMEM);
	CHECK(!drv_busy() && !paula_busy && !drv_active());
	if (drv_module())
		CHECK(fake_int2 == 0);
	fake_kmem_fail = 0;
	CHECK(drv_open() == 0);
	CHECK(drv_close() == 0);
}

/* a second open while the first sleeps in intr_attach */
static int second;

static void
open_second(void)
{
	second = drv_open();
}

static void
t_open_race(void)
{
	if (!drv_module())
		return;
	start("open_race");
	second = -1;
	fake_on_attach = open_second;
	CHECK(drv_open() == 0);
	CHECK(second == EBUSY);
	CHECK(drv_busy() && fake_int2 != 0);
	CHECK(drv_close() == 0);
	CHECK(fake_int2 == 0 && !paula_busy);
}

/* the clock runs a timeout made below spl4 before the sleep */
static void
t_selftest_race(void)
{
	if (!drv_module())
		return;
	start("selftest_race");
	fake_race = 1;
	CHECK(drv_open() == 0);
	CHECK(fake_hung == 0);
	CHECK(fake_ipl == 0);
	CHECK(drv_close() == 0);
}

static void
t_selftest_noint(void)
{
	if (!drv_module())
		return;
	start("selftest_noint");
	fake_on_tick = 0;		/* timer B never reaches the chain */
	CHECK(drv_open() == EIO);
	CHECK(fake_sleeps <= 25 && fake_hung == 0);
	CHECK(!drv_busy() && !paula_busy && !drv_active());
	CHECK(fake_int2 == 0);
	CHECK(fake_kmem_live == 0 && fake_chip_live == 0);
	CHECK(fake_ipl == 0);
}

/* ProTracker's order: off, set, wait, on, wait, loop part */
static void
t_tick_order(void)
{
	struct paula_tick *t;
	int i;
	long toff = -1, ton = -1, tlc = -1;

	start("tick_order");
	CHECK(drv_open() == 0);
	CHECK(setup(1024) == 0);
	t = utick();
	op(t, PO_DMA, 0, 0x0001, 0);
	op(t, PO_LEN, 0, 16, 0);
	op(t, PO_LC, 0, 0, 0);
	op(t, PO_PER, 0, 428, 0);
	op(t, PO_VOL, 0, 48, 0);
	op(t, PO_DMA, 0, 0x8001, 0);
	op(t, PO_LC, 0, 0, 32);
	op(t, PO_LEN, 0, 8, 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	fake_nev = 0;
	drv_timer();
	hw_step();
	for (i = 0; i < fake_nev; i++) {
		struct ev *e = &fake_ev[i];

		if (e->kind == EV_DMACON && e->v == 0x0001)
			toff = e->t;
		if (e->kind == EV_DMACON && e->v == 0x8001)
			ton = e->t;
		if (e->kind == EV_LC && e->ch == 0 && e->v == 16 + 32)
			tlc = e->t;
	}
	CHECK(toff >= 0 && ton > toff && tlc > ton);
	/* wait_lines(5) counts 5 line changes: more than 4 whole lines */
	CHECK(ton - toff > 4 * UNITS_PER_LINE);	/* Paula sees the off */
	CHECK(tlc - ton > 4 * UNITS_PER_LINE);		/* and latches */
	CHECK(fake_amiga.audio[0].period == 428);
	CHECK(fake_amiga.audio[0].loud == 48);
	CHECK(fake_amiga.audio[0].size == 8);
	CHECK(drv_close() == 0);
}

/* 24 off/on pairs in one tick: two waits at most, not 48 */
static void
t_dma_bound(void)
{
	struct paula_tick *t;
	long t0, lines;
	int i;

	start("dma_bound");
	CHECK(drv_open() == 0);
	CHECK(setup(1024) == 0);
	t = utick();
	for (i = 0; i < PAULA_MAXOPS / 2; i++) {
		op(t, PO_DMA, 0, 1 << (i & 3), 0);
		op(t, PO_DMA, 0, 0x8000 | 1 << (i & 3), 0);
	}
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	t0 = fake_now;
	drv_timer();
	lines = (fake_now - t0) / UNITS_PER_LINE;
	CHECK(lines <= 2 * 5 + 10);	/* two waits and the ops themselves */
	fprintf(stdout, "  dma_bound: %ld lines for 24 off/on pairs\n", lines);
	CHECK(drv_close() == 0);
}

/* one PO_DMA per channel, each with its own loop part: one DMA on for
   all four, every loop pointer written after it has been latched */
static void
t_per_channel(void)
{
	struct paula_tick *t;
	int c, i, nset = 0, nloop = 0, late = 1;
	long toff = -1, ton = -1;

	start("per_channel");
	CHECK(drv_open() == 0);
	CHECK(setup(1024) == 0);
	t = utick();
	for (c = 0; c < 4; c++) {
		op(t, PO_DMA, c, 1 << c, 0);
		op(t, PO_LEN, c, 16, 0);
		op(t, PO_LC, c, 0, 0);
		op(t, PO_PER, c, 300 + c, 0);
		op(t, PO_VOL, c, 40, 0);
		op(t, PO_DMA, c, 0x8000 | 1 << c, 0);
		op(t, PO_LC, c, 0, 32);
		op(t, PO_LEN, c, 8, 0);
	}
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	fake_nev = 0;
	drv_timer();
	hw_step();
	for (i = 0; i < fake_nev; i++) {
		struct ev *e = &fake_ev[i];

		if (e->kind == EV_DMACON && !(e->v & 0x8000))
			toff = e->t;
		if (e->kind == EV_DMACON && (e->v & 0x8000)) {
			nset++;
			ton = e->t;
			CHECK(e->v == 0x800f);
		}
		if (e->kind == EV_LC && e->v == 16 + 32) {
			nloop++;
			if (ton < 0 || e->t - ton <= 4 * UNITS_PER_LINE)
				late = 0;
		}
	}
	CHECK(nset == 1);
	CHECK(ton - toff > 4 * UNITS_PER_LINE);
	CHECK(nloop == 4 && late);
	for (c = 0; c < 4; c++) {
		CHECK(fake_amiga.audio[c].size == 8);
		CHECK(fake_amiga.audio[c].period == 300 + c);
	}
	CHECK(drv_close() == 0);
}

/* a tick is checked before it is queued */
static int
queued(void)
{
	struct paula_status *st = (struct paula_status *)(user + 12288);

	drv_ioctl(PAULA_STATUS, (long)st);
	return (int)st->queued;
}

static void
t_push_checks(void)
{
	struct paula_tick *t;

	start("push_checks");
	CHECK(drv_open() == 0);
	t = utick();
	op(t, 8, 0, 0, 0);				/* no such op */
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == EINVAL);
	t = utick();
	op(t, PO_VOL, 4, 10, 0);			/* no channel 4 */
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == EINVAL);
	t = utick();
	op(t, PO_LC, 0, 0, 33);				/* odd offset */
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == EINVAL);
	t = utick();
	t->nops = PAULA_MAXOPS + 1;
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == EINVAL);
	CHECK(queued() == 0);
	t = utick();
	op(t, PO_LC, 0, 0, PAULA_NULLOFF);
	op(t, PO_LC, 1, 0, 32);
	op(t, PO_TEMPO, 0, 125, 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	CHECK(queued() == 1);
	CHECK(drv_close() == 0);
}

/* a second PUSH, or a SETUP, while a call sleeps in copyin */
static int nested_cmd, nested_rv;
static long nested_arg;

static void
nested(void)
{
	nested_rv = drv_ioctl(nested_cmd, nested_arg);
}

static void
arm_second(void)
{
	fake_on_copyin = nested;
}

static void
t_one_at_a_time(void)
{
	struct paula_tick *t;
	struct paula_load *ld = (struct paula_load *)(user + 4096);
	int n;

	start("one_at_a_time");
	CHECK(drv_open() == 0);
	t = utick();
	op(t, PO_VOL, 0, 10, 0);
	nested_cmd = PAULA_PUSH;
	nested_arg = (long)t;
	nested_rv = -1;
	fake_on_copyin = nested;
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	CHECK(nested_rv == EAGAIN);
	CHECK(queued() == 1);
	/* LOAD: the first copyin (of ld) runs before the lock, the second
	   (the samples) inside it: a SETUP there gets EAGAIN */
	CHECK(drv_ioctl(PAULA_SETUP, 1024L) == 0);
	ld->off = 0;
	ld->len = 64;
	ld->data = user + 8192;
	for (n = 0; n < 2; n++) {
		nested_cmd = PAULA_SETUP;
		nested_arg = 2048;
		nested_rv = -1;
		fake_on_copyin = 0;
		if (n == 0)
			fake_on_copyin = nested;	/* in the copyin of ld */
		CHECK(drv_ioctl(PAULA_LOAD, (long)ld) == 0);
		if (n == 0)
			CHECK(nested_rv == 0);
	}
	nested_cmd = PAULA_SETUP;
	nested_arg = 2048;
	nested_rv = -1;
	fake_on_copyin = arm_second;	/* in the samples' copyin */
	CHECK(drv_ioctl(PAULA_LOAD, (long)ld) == 0);
	CHECK(nested_rv == EAGAIN);
	CHECK(drv_close() == 0);
}

/* a DMA on before PAULA_SETUP points Paula at nothing */
static void
t_no_area(void)
{
	struct paula_tick *t;
	int i, set = 0;

	start("no_area");
	CHECK(drv_open() == 0);
	t = utick();
	op(t, PO_LC, 0, 0, 0);
	op(t, PO_LEN, 0, 16, 0);
	op(t, PO_DMA, 0, 0x8001, 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	fake_nev = 0;
	drv_timer();
	hw_step();
	for (i = 0; i < fake_nev; i++)
		if ((fake_ev[i].kind == EV_DMACON && (fake_ev[i].v & 0x8000)) ||
		    fake_ev[i].kind == EV_LC)
			set++;
	CHECK(set == 0);
	CHECK(fake_amiga.audio[0].buf == 0);
	CHECK(drv_close() == 0);
}

/* the LED and filter bit goes back at close, and at halt */
static void
t_led(void)
{
	struct paula_tick *t;

	start("led");
	fake_acia.pra = 0x03;			/* LED dim, filter out; OVL */
	CHECK(drv_open() == 0);
	t = utick();
	op(t, PO_LED, 0, 1, 0);
	op(t, PO_VOL, 2, 40, 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	drv_timer();
	CHECK(fake_acia.pra == 0x01);
	CHECK(drv_close() == 0);
	CHECK(fake_acia.pra == 0x03);
	if (!drv_module())
		return;
	start("halt");
	fake_acia.pra = 0x03;
	CHECK(drv_open() == 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	drv_timer();
	CHECK(fake_acia.pra == 0x01 && fake_amiga.audio[2].loud == 40);
	drv_halt();
	CHECK(fake_acia.pra == 0x03);
	CHECK(fake_amiga.audio[2].loud == 0);
	CHECK(!drv_active() && fake_acia.crb == 0);
	CHECK(drv_close() == 0);	/* the test goes on; a box would not */
}

/* PAULA_WAIT sleeps until the queue is down to n; a signal ends it */
static void
t_wait(void)
{
	struct paula_tick *t;
	int i;

	start("wait");
	CHECK(drv_open() == 0);
	t = utick();
	for (i = 0; i < 3; i++)
		CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	fake_sleeps = 0;
	CHECK(drv_ioctl(PAULA_WAIT, 1L) == 0);
	CHECK(queued() == 1 && fake_sleeps == 2 && fake_hung == 0);
	CHECK(drv_ioctl(PAULA_WAIT, 5L) == 0);	/* already there */
	CHECK(fake_sleeps == 2);
	fake_on_tick = 0;			/* the queue does not move */
	fake_signal = 1;
	CHECK(drv_ioctl(PAULA_WAIT, 0L) == EINTR);
	CHECK(fake_ipl == 0);
	CHECK(drv_close() == 0);
}

/* idle counts underruns only: not the self-test's tick, not the ticks
   before the first push */
static void
t_idle(void)
{
	struct paula_tick *t;
	struct paula_status *st = (struct paula_status *)(user + 12288);

	start("idle");
	CHECK(drv_open() == 0);
	drv_timer();
	drv_timer();
	CHECK(drv_ioctl(PAULA_STATUS, (long)st) == 0);
	CHECK(st->idle == 0 && st->played == 0);
	t = utick();
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	drv_timer();			/* plays it */
	drv_timer();			/* an underrun */
	CHECK(drv_ioctl(PAULA_STATUS, (long)st) == 0);
	CHECK(st->played == 1 && st->idle == 1);
	CHECK(drv_ioctl(PAULA_RESET, 0L) == 0);
	drv_timer();
	CHECK(drv_ioctl(PAULA_STATUS, (long)st) == 0);
	CHECK(st->idle == 1);
	CHECK(drv_close() == 0);
}

static void
t_queue_full(void)
{
	struct paula_tick *t;
	int i, e = 0;

	start("queue_full");
	CHECK(drv_open() == 0);
	t = utick();
	for (i = 0; i < PAULA_RING - 1; i++)
		e |= drv_ioctl(PAULA_PUSH, (long)t);
	CHECK(e == 0);
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == EAGAIN);
	drv_timer();
	CHECK(drv_ioctl(PAULA_PUSH, (long)t) == 0);
	CHECK(drv_close() == 0);
}

/* the area is cleared, the guard too, and nothing past the request */
static void
t_chip_clear(void)
{
	char *c;
	int i, dirty = 0;

	start("chip_clear");
	CHECK(drv_open() == 0);
	CHECK(drv_ioctl(PAULA_SETUP, 1026L) == 0);	/* 1026 + 16 = 2 mod 4 */
	c = drv_chip();
	for (i = 0; i < 1026 + 16; i++)
		dirty |= c[i];
	CHECK(dirty == 0);
	CHECK(fake_chip_overrun() == 0);
	CHECK(drv_ioctl(PAULA_SETUP, 0L) == 0);
	CHECK(fake_chip_live == 0);
	CHECK(drv_close() == 0);
}

int
main(void)
{
	user = mmap(0, 1 << 20, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (user == MAP_FAILED) {
		perror("mmap");
		return 2;
	}
	t_open_close();
	t_exclusion();
	t_clock();
	t_nosleep();
	t_open_race();
	t_selftest_race();
	t_selftest_noint();
	t_tick_order();
	t_dma_bound();
	t_per_channel();
	t_push_checks();
	t_one_at_a_time();
	t_no_area();
	t_led();
	t_wait();
	t_idle();
	t_queue_full();
	t_chip_clear();
	fprintf(stdout, "%s: %d checks, %d failed\n",
	    drv_module() ? "module" : "static", checks, fails);
	return fails != 0;
}
