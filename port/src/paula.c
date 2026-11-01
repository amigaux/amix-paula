/*
 * paula.c -- /dev/paula (major 53): Paula's four audio channels for one
 * program, replayed tick by tick from CIA-A timer B (AMIX 68040/68060 port,
 * 2026-10-27).  The interface is in paula.h.
 *
 * Why: a tracker on AMIX had only /dev/audio, a sample stream, so it mixed
 * all four voices on the CPU -- 16-19 us per output sample in pt2-clone's
 * mixer on the 68060, half of real time.  On AmigaOS a ProTracker replayer
 * mixes nothing: it writes Paula's registers once per tick and Paula plays
 * the samples from chip RAM.  This driver gives a program that again.
 *
 * Timing: CIA-A timer B, free in this kernel (the clock is CIA-A timer A,
 * CIA-B is not used and its level-6 input EXTER stays disabled -- an
 * unacknowledged Zorro /INT6 there storms, see amix-z3660net), runs at
 * 709379 (PAL) or 715909 (NTSC) * 2.5 / BPM, as ProTracker's CIA timing.
 * Each underflow applies one queued tick from the level-2 interrupt
 * (aciaaintr, replaced here: keyboard, parallel and clock exactly as stock,
 * then timer B).  Within a tick the ProTracker order is kept: channels
 * switched off, registers set, a wait of a few scanlines so Paula notices,
 * DMA on, a few scanlines more so Paula has latched pointer and length,
 * then the loop pointer and length for the next cycle.  The waits poll the
 * beam counter (VHPOSR), as players do; they cost under 1 ms of IPL 2 per
 * tick, and IPL 5 (serial) still preempts them.
 *
 * Concurrency: the queue is a single-producer ring -- PAULA_PUSH (process
 * context) writes ring[head] then advances head; the tick (IPL 2) reads
 * ring[tail] then advances tail.  Everything else that the interrupt can
 * see is changed with the interrupt masked (spl4 covers IPL 2).  The
 * interrupt calls nothing but this file and the hardware.
 *
 * /dev/audio and /dev/paula exclude each other: patch_audio_paula.py makes
 * audioopen() refuse while paula_busy is set and counts its own clients in
 * audio_nopen, which paulaopen() checks.  Both globals live in the patched
 * audio.c (it is linked first).  Each open tests the other's flag and sets
 * its own with no sleep between: paula_claim() comes before anything that
 * can sleep, and audioopen() tests paula_busy again after its allocation.
 *
 * Two forms, one source.  Built plainly, this is the static driver that
 * relink-040-paula.sh links into a 68040/68060 kernel.  With -DPAULA_MODULE
 * (module/paula_mod.c includes this file) it is a loadable module for the
 * Amix module system: aciaaintr is left out, and timer B runs one-shot,
 * restarted by the module's level-2 handler (module/README.md says why).
 *
 * Original code for this port; no Commodore source.
 */
#include "sys/types.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/inline.h"
#include "sys/errno.h"
#include "sys/systm.h"
#include "sys/kmem.h"
#include "sys/psw.h"
#include "sys/pcb.h"
#include "memory.h"
#include "amigahr.h"
#include "paula.h"

extern int paula_busy;		/* patched audio.c */
extern int audio_nopen;		/* patched audio.c */
extern unsigned int displaytype;
#ifndef PAULA_MODULE
extern void kbintr();
extern void parintr();
extern int clock_int();
extern void addupc_clk();
#endif

#define CHIPGUARD	16	/* zero bytes before the sample area: PAULA_NULLOFF */
#define SCANLINES	5	/* ProTracker's DMA wait, ~320 us */
#define CRB_START	0x01
#ifndef PAULA_MODULE
#define CRB_LOAD	0x10	/* continuous mode, E clock (RUNMODE, INMODE 0) */
#else
#define CRB_LOAD	0x18	/* one-shot (RUNMODE 1), E clock: module/paula_mod.c */
#endif

static struct paula_tick *ring;
static volatile int head, tail;	/* ring[tail..head-1] are queued */
static char *chip;		/* CHIPGUARD + area bytes of chip RAM */
static unsigned long area;
static unsigned long played, idle;
static unsigned long ticks;	/* timer B ticks taken while active */
static int fed;			/* pushed to since the queue was last dropped */
static int busy;
static int active;		/* timer B interrupts on */
#ifdef PAULA_MODULE
static unsigned short tb_latch;	/* the period the next one-shot restart loads */
#endif

/* per channel: where the sample pointer and length are, to keep the pair
   inside the area whichever is written first */
static unsigned long ch_off[4];
static unsigned long ch_len[4];

static unsigned short
tempo_latch(bpm)
int bpm;
{
	unsigned long e = PAL ? 709379L : 715909L;

	if (bpm < 32)
		bpm = 32;
	if (bpm > 255)
		bpm = 255;
	return (unsigned short)(e * 5 / 2 / bpm);
}

static void
set_tempo(bpm)
int bpm;
{
	unsigned short n = tempo_latch(bpm);

#ifdef PAULA_MODULE
	/* one-shot: a write to tbhi would restart a running timer; the
	   level-2 handler loads tb_latch when it restarts it */
	tb_latch = n;
	if (active)
		return;
#endif
	/* the latch; a running continuous timer reloads it at its next underflow */
	ACIAA->tblo = n & 0xff;
	ACIAA->tbhi = n >> 8;
}

/* wait for the beam to move n lines (bounded: the beam always runs, but a
   hang at IPL 2 must not be possible) */
static void
wait_lines(n)
int n;
{
	unsigned short v = AMIGA->vhposr >> 8;
	long guard = 200000L;

	while (n > 0 && --guard > 0) {
		unsigned short w = AMIGA->vhposr >> 8;
		if (w != v) {
			v = w;
			n--;
		}
	}
}

/* pointer and length of channel c, kept inside the area */
static void
put_sample(c)
int c;
{
	volatile struct amiga_audio *a = &AMIGA->audio[c];
	unsigned long off = ch_off[c], words = ch_len[c], room;

	if (off == PAULA_NULLOFF || off >= area) {
		a->buf = (__signed__ char *)chip;
		a->size = 1;
		return;
	}
	room = (area - off) / 2;
	if (words == 0)
		words = 65536L;
	if (words > room)
		words = room;
	if (words == 0) {
		a->buf = (__signed__ char *)chip;
		a->size = 1;
		return;
	}
	a->buf = (__signed__ char *)(chip + CHIPGUARD + off);
	a->size = (unsigned short)words;	/* 65536 is written as 0 */
}

static void
channels_off()
{
	int c;

	AMIGA->dmacon = DMACLR | DMAAUD0 | DMAAUD1 | DMAAUD2 | DMAAUD3;
	for (c = 0; c < 4; c++) {
		AMIGA->audio[c].loud = 0;
		ch_off[c] = PAULA_NULLOFF;
		ch_len[c] = 1;
	}
}

/* one queued tick, from the level-2 interrupt */
static void
paula_tick()
{
	struct paula_tick *t;
	struct paula_op *o;
	int i, n, c, cleared = 0;

	if (!active)
		return;
	ticks++;
	if (tail == head) {
		if (fed)	/* an underrun, not the wait for the first push */
			idle++;
		return;
	}
	t = &ring[tail];
	n = t->nops;
	for (i = 0, o = t->op; i < n; i++, o++) {
		c = o->ch & 3;
		switch (o->op) {
		case PO_LC:
			ch_off[c] = o->off;
			put_sample(c);
			break;
		case PO_LEN:
			ch_len[c] = o->val;
			put_sample(c);
			break;
		case PO_PER:
			AMIGA->audio[c].period = o->val < 113 ? 113 : o->val;
			break;
		case PO_VOL:
			AMIGA->audio[c].loud = o->val > 64 ? 64 : o->val;
			break;
		case PO_DMA:
			if (o->val & 0x8000) {
				if (cleared)	/* let Paula see the channel off */
					wait_lines(SCANLINES);
				AMIGA->dmacon = DMASET | (o->val & 0xf);
				/* the loop pointer and length that follow are for
				   the next cycle: Paula must have latched these */
				wait_lines(SCANLINES);
				cleared = 0;
			} else {
				AMIGA->dmacon = DMACLR | (o->val & 0xf);
				cleared = 1;
			}
			break;
		case PO_LED:
			if (o->val)
				ACIAA->pra &= ~2;	/* power LED on: filter in */
			else
				ACIAA->pra |= 2;
			break;
		case PO_TEMPO:
			set_tempo((int)o->val);
			break;
		}
	}
	tail = (tail + 1) % PAULA_RING;
	played++;
}

/*
 * The level-2 CIA-A handler: the stock one (keyboard, parallel port, clock
 * on timer A, in that order) and timer B for /dev/paula.  Reading icr
 * clears every pending bit, so there is one reader.
 */
#ifndef PAULA_MODULE
void
aciaaintr(frame)
pcb_t *frame;
{
	register unsigned char icr = ACIAA->icr;	/* one read: all bits */

	if (icr & ICR_SP)
		kbintr();
	if (icr & ICR_FLG)
		parintr();
	if ((icr & ICR_TA) && clock_int(frame))
		addupc_clk(frame);
	if (icr & ICR_TB)
		paula_tick();
}
#endif /* PAULA_MODULE */

static void
free_area()
{
	if (chip) {
		FreeMem(chip, area + CHIPGUARD);
		chip = 0;
	}
	area = 0;
}

static void
stop_all()
{
	int s = spl4();

	ACIAA->icr = ICR_CLR | ICR_TB;
	ACIAA->crb = 0;
	active = 0;
	channels_off();
	tail = head;
	fed = 0;
	splx(s);
}

/*
 * The channels: tested and claimed with no sleep between, so of two opens
 * (of /dev/paula, or one of /dev/audio) one wins and the other gets EBUSY.
 * Everything that can sleep comes after the claim.
 */
static int
paula_claim()
{
	if (busy || audio_nopen)
		return EBUSY;
	busy = 1;
	paula_busy = 1;
	return 0;
}

static void
paula_release()
{
	busy = 0;
	paula_busy = 0;
}

/* the open's work once the channels are claimed */
static int
paula_start()
{
	int s;

	ring = (struct paula_tick *)kmem_alloc(sizeof (struct paula_tick) * PAULA_RING, 0);
	if (!ring)
		return ENOMEM;
	head = tail = 0;
	played = idle = ticks = 0;
	fed = 0;
	chip = 0;
	area = 0;

	s = spl4();
	/* the channels are ours: no audio interrupts (Paula loops by itself) */
	AMIGA->intena = AINTCLR | AIEAUD0 | AIEAUD1 | AIEAUD2 | AIEAUD3;
	AMIGA->intreq = AINTCLR | AIEAUD0 | AIEAUD1 | AIEAUD2 | AIEAUD3;
	channels_off();
	/* timer B: 125 BPM until told otherwise, continuous, interrupting */
	ACIAA->crb = 0;
	set_tempo(125);
	active = 1;
	ACIAA->icr = ICR_SET | ICR_TB;
	ACIAA->crb = CRB_START | CRB_LOAD;
	splx(s);
	return 0;
}

int
paulaopen(dp, oflag, otyp, crp)
dev_t *dp;
int oflag, otyp;
struct cred *crp;
{
	int e;

	if ((e = paula_claim()) != 0)
		return e;
	if ((e = paula_start()) != 0)
		paula_release();
	return e;
}

int
paulaclose(d, oflag, otyp, crp)
dev_t d;
int oflag, otyp;
struct cred *crp;
{
	stop_all();
	free_area();
	kmem_free((caddr_t)ring, sizeof (struct paula_tick) * PAULA_RING);
	ring = 0;
	paula_release();
	return 0;
}

int
paulaioctl(d, cmd, arg, oflag, crp, rvp)
dev_t d;
int cmd, arg, oflag;
struct cred *crp;
int *rvp;
{
	struct paula_load ld;
	struct paula_status st;
	struct paula_tick *t;
	unsigned long size;
	int s, next;

	switch (cmd) {
	case PAULA_SETUP:
		size = (unsigned long)arg;
		if (size > PAULA_MAXAREA)
			return EINVAL;
		/* nothing may still play from the old area */
		s = spl4();
		channels_off();
		tail = head;
		fed = 0;
		splx(s);
		free_area();
		if (size == 0)
			return 0;
		size = (size + 1) & ~1L;
		/* MEMF_CHIP only: AllocMem leaves chip memory as it was, with
		   MEMF_CLEAR or without (it clears only what it takes from
		   kmem) */
		chip = (char *)AllocMem(size + CHIPGUARD, MEMF_CHIP);
		if (!chip)
			return ENOMEM;
		/* so clear it: the guard is the silent word, and a channel
		   pointed at a part not yet loaded plays silence.  32-bit
		   words, then the last 16 bits when the even size leaves them;
		   nothing past the request */
		{
			register unsigned int *p = (unsigned int *)chip;
			register unsigned long n = (size + CHIPGUARD) / 4;

			while (n-- > 0)
				*p++ = 0;
			if ((size + CHIPGUARD) & 2)
				*(unsigned short *)p = 0;
		}
		area = size;
		return 0;

	case PAULA_LOAD:
		if (copyin((caddr_t)arg, (caddr_t)&ld, sizeof ld))
			return EFAULT;
		if (!chip || ld.off > area || ld.len > area - ld.off)
			return EINVAL;
		if (copyin((caddr_t)ld.data, (caddr_t)(chip + CHIPGUARD + ld.off), ld.len))
			return EFAULT;
		return 0;

	case PAULA_PUSH:
		next = (head + 1) % PAULA_RING;
		if (next == tail)
			return EAGAIN;
		t = &ring[head];
		if (copyin((caddr_t)arg, (caddr_t)t, 4))
			return EFAULT;
		if (t->nops > PAULA_MAXOPS)
			return EINVAL;
		if (t->nops && copyin((caddr_t)&((struct paula_tick *)arg)->op[0],
		    (caddr_t)t->op, t->nops * sizeof (struct paula_op)))
			return EFAULT;
		head = next;		/* the tick may take it from here on */
		fed = 1;
		return 0;

	case PAULA_STATUS:
		s = spl4();
		st.played = played;
		st.idle = idle;
		st.queued = (head - tail + PAULA_RING) % PAULA_RING;
		st.area = area;
		splx(s);
		if (copyout((caddr_t)&st, (caddr_t)arg, sizeof st))
			return EFAULT;
		return 0;

	case PAULA_RESET:
		s = spl4();
		tail = head;
		fed = 0;
		channels_off();
		splx(s);
		return 0;
	}
	return EINVAL;
}
