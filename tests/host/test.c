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
	t_open_race();
	t_selftest_race();
	t_selftest_noint();
	t_tick_order();
	t_queue_full();
	t_chip_clear();
	fprintf(stdout, "%s: %d checks, %d failed\n",
	    drv_module() ? "module" : "static", checks, fails);
	return fails != 0;
}
