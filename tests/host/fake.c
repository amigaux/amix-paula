/*
 * fake.c -- the host model of the hardware and kernel the driver runs
 * against in the host tests: custom chips and CIA-A as plain memory with a
 * beam that moves one scan line per UNITS_PER_LINE register accesses, and
 * the kernel calls paula.c and paula_mod.c make.
 *
 * sleep() and timeout() model the lost wakeup: a timeout() made below
 * spl4 with fake_race set fires at once (the clock got in before the
 * sleep), and a sleep() with no wakeup to come counts in fake_hung instead
 * of hanging the test.
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "sys/types.h"
#include "amigahr.h"
#include "fake.h"

struct amiga fake_amiga;
struct acia fake_acia;
long fake_now;
struct ev fake_ev[4096];
int fake_nev;
int fake_ipl;
int paula_busy, audio_nopen;
unsigned int displaytype = 2;

int fake_kmem_flag = -1, fake_kmem_fail, fake_kmem_live;
int fake_chip_live, fake_chip_fail;
int fake_race, fake_hung, fake_sleeps, fake_signal;
int (*fake_int2)();
void (*fake_on_tick)();
int fake_printfs;
unsigned char *fake_chip_base;
static unsigned long chip_size;
#define CANARY	64

static signed char *last_buf[4];

struct callout { void (*fn)(); caddr_t arg; };
static struct callout callouts[16];
static int ncallouts;
static caddr_t woken;

void
fake_reset(void)
{
	memset(&fake_amiga, 0, sizeof fake_amiga);
	memset(&fake_acia, 0, sizeof fake_acia);
	fake_amiga.dmacon = DMACON_IDLE;
	fake_amiga.vposr = 0x2000;	/* ECS Agnus, PAL */
	memset(last_buf, 0, sizeof last_buf);
	fake_now = 0;
	fake_nev = 0;
	fake_ipl = 0;
	paula_busy = audio_nopen = 0;
	displaytype = 2;
	fake_kmem_flag = -1;
	fake_kmem_fail = fake_chip_fail = 0;
	fake_race = fake_hung = fake_sleeps = fake_signal = 0;
	fake_on_tick = 0;
	fake_printfs = 0;
	ncallouts = 0;
	woken = 0;
}

long
fake_line(void)
{
	return fake_now / UNITS_PER_LINE;
}

static void
log_ev(int kind, unsigned v, int ch)
{
	if (fake_nev < (int)(sizeof fake_ev / sizeof fake_ev[0])) {
		fake_ev[fake_nev].t = fake_now;
		fake_ev[fake_nev].kind = kind;
		fake_ev[fake_nev].v = v;
		fake_ev[fake_nev].ch = ch;
		fake_nev++;
	}
}

void
hw_step(void)
{
	int c;
	long line;

	if (fake_amiga.dmacon != DMACON_IDLE) {
		log_ev(EV_DMACON, fake_amiga.dmacon, -1);
		fake_amiga.dmacon = DMACON_IDLE;
	}
	for (c = 0; c < 4; c++)
		if (fake_amiga.audio[c].buf != last_buf[c]) {
			last_buf[c] = fake_amiga.audio[c].buf;
			log_ev(EV_LC, (unsigned)((unsigned char *)last_buf[c] - fake_chip_base), c);
		}
	fake_now++;
	line = fake_now / UNITS_PER_LINE;
	fake_amiga.vhposr = (unsigned short)(((line & 0xff) << 8) | (fake_now % UNITS_PER_LINE));
}

/* kernel memory */
caddr_t
kmem_alloc(unsigned long n, int flag)
{
	char *p;

	fake_kmem_flag = flag;
	if (fake_kmem_fail)
		return 0;
	p = malloc(n);
	memset(p, 0x55, n);
	fake_kmem_live++;
	return p;
}

void
kmem_free(caddr_t p, unsigned long n)
{
	(void)n;
	free(p);
	fake_kmem_live--;
}

/* chip memory: not cleared, with a canary after the request */
char *
AllocMem(unsigned long n, int flags)
{
	(void)flags;
	if (fake_chip_fail)
		return 0;
	fake_chip_base = malloc(n + CANARY);
	memset(fake_chip_base, 0xaa, n + CANARY);
	chip_size = n;
	fake_chip_live++;
	return (char *)fake_chip_base;
}

void
FreeMem(char *p, unsigned long n)
{
	if ((unsigned char *)p != fake_chip_base || n != chip_size) {
		fprintf(stderr, "FreeMem: %p %lu, allocated %p %lu\n", (void *)p, n,
		    (void *)fake_chip_base, chip_size);
		abort();
	}
	free(p);
	fake_chip_base = 0;
	fake_chip_live--;
}

int
fake_chip_overrun(void)
{
	int i, n = 0;

	for (i = 0; i < CANARY; i++)
		if (fake_chip_base[chip_size + i] != 0xaa)
			n++;
	return n;
}

int
copyin(caddr_t from, caddr_t to, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}

int
copyout(caddr_t from, caddr_t to, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}

/* timeouts, sleep and wakeup */
int
timeout(void (*fn)(), caddr_t arg, int ticks)
{
	(void)ticks;
	if (fake_race && fake_ipl < 4) {
		fn(arg);	/* the clock ran it before the caller slept */
		return 1;
	}
	if (ncallouts < 16) {
		callouts[ncallouts].fn = fn;
		callouts[ncallouts].arg = arg;
		ncallouts++;
	}
	return 1;
}

void
wakeup(caddr_t chan)
{
	woken = chan;
}

static void
clock_tick(void)
{
	int i, n = ncallouts;

	if (fake_on_tick)
		fake_on_tick();
	ncallouts = 0;
	for (i = 0; i < n; i++)
		callouts[i].fn(callouts[i].arg);
}

int
sleep(caddr_t chan, int pri)
{
	int ipl = fake_ipl, i, pending = 0;

	fake_sleeps++;
	woken = 0;
	for (i = 0; i < ncallouts; i++)
		if (callouts[i].arg == chan)
			pending = 1;
	fake_ipl = 0;		/* the level falls while asleep */
	clock_tick();
	fake_ipl = ipl;
	if (woken == chan)
		return 0;
	if ((pri & 0400) && fake_signal)
		return 1;
	if (!pending)
		fake_hung++;	/* nothing would have woken it */
	return 0;
}

int
intr_attach(int level, int (*fn)(), char *arg, char *name)
{
	(void)level; (void)arg; (void)name;
	if (fake_int2)
		return EEXIST;
	fake_int2 = fn;
	return 0;
}

int
intr_detach(int level, int (*fn)(), char *arg)
{
	(void)level; (void)arg;
	if (fake_int2 == fn)
		fake_int2 = 0;
	return 0;
}

int
printf(const char *fmt, ...)
{
	(void)fmt;
	fake_printfs++;
	return 0;
}

/* the static form's level-2 neighbours */
void kbintr(void) {}
void parintr(void) {}
int clock_int(void *f) { (void)f; return 0; }
void addupc_clk(void *f) { (void)f; }
