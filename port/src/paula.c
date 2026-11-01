/*
 * paula.c -- /dev/paula (major 54): Paula's four audio channels for one
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
 * Which of the two: pick_clock() says how it is chosen.
 * Each underflow applies one queued tick from the level-2 interrupt
 * (aciaaintr, replaced here: keyboard, parallel and clock exactly as stock,
 * then timer B).  Within a tick the ProTracker order is kept: channels
 * switched off, registers set, a wait of a few scanlines so Paula notices,
 * DMA on, a few scanlines more so Paula has latched pointer and length,
 * then the loop pointer and length for the next cycle.  The waits poll the
 * beam counter (VHPOSR), as players do.  There are at most two per tick
 * whatever the tick holds (paula_tick says how), so at most 2 * SCANLINES
 * lines, about 0.64 ms, of IPL 2 per tick; IPL 5 (serial) still preempts
 * them.
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
extern unsigned int displaytype;	/* the PAL macro, sys/param.h */
#ifndef PAULA_MODULE
extern void kbintr();
extern void parintr();
extern int clock_int();
extern void addupc_clk();
#endif

#define CHIPGUARD	16	/* zero bytes before the sample area: PAULA_NULLOFF */
#define CLOCK_PAL	3546895L	/* Paula's clock, Hz; the E clock is a fifth */
#define CLOCK_NTSC	3579545L
#define VPOSR_NTSC	0x1000	/* Agnus/Alice id bit 4: an NTSC part */
#define SCANLINES	5	/* ProTracker's DMA wait, ~320 us */
#define PERIOD_MIN	113	/* ProTracker's highest note, B-3 */
#define VOLUME_MAX	64
#define DMA_AUDIO	(DMAAUD0 | DMAAUD1 | DMAAUD2 | DMAAUD3)	/* DMACON bits 0-3 */
#define AIE_AUDIO	(AIEAUD0 | AIEAUD1 | AIEAUD2 | AIEAUD3)
#define PRA_LED		0x02	/* CIA-A PRA bit 1, /LED: low = power LED
				   bright and the audio low-pass filter in */
/* CIA control register B: timer B counts the E clock (INMODE 0) */
#define CRB_START	0x01
#define CRB_RUNMODE	0x08	/* one-shot: an underflow stops the timer */
#define CRB_LOAD	0x10	/* force load: the latch into the counter */
#ifndef PAULA_MODULE
#define CRB_RUN		(CRB_START | CRB_LOAD)			/* continuous */
#else
#define CRB_RUN		(CRB_START | CRB_RUNMODE | CRB_LOAD)	/* module/paula_mod.c */
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
static int clock_set;		/* 0 auto, 1 NTSC, 2 PAL (the module's parameter) */
static unsigned char led_was;	/* PRA_LED at open, put back at close */
static unsigned long clock_flags;	/* PI_* of the clock in use */

/* per channel: where the sample pointer and length are, to keep the pair
   inside the area whichever is written first */
static unsigned long ch_off[4];
static unsigned long ch_len[4];

/*
 * PAL or NTSC: the crystal behind Paula's clock and the CIA E clock, which
 * no register reports.  Two hints:
 *  - the Agnus/Alice id in VPOSR (bits 8-14), whose bit 4 is set on an
 *    NTSC part: the chip and its strapping, not a mode software sets;
 *  - displaytype, which the kernel sets at boot by counting E-clock ticks
 *    over one video field: 50 or 60 Hz fields, the beam's mode, which an
 *    ECS or AGA chip can be switched to whatever the crystal (the boot
 *    menu's PAL/NTSC choice).  The PAL macro reads it.
 * The id is taken; both are reported (PAULA_INFO), and the module's clock
 * parameter overrides them for a machine whose crystal is not the chip's.
 */
static void
pick_clock()
{
	unsigned long f = 0;

	if (AMIGA->vposr & VPOSR_NTSC)
		f |= PI_IDNTSC;
	if (!PAL)
		f |= PI_FIELD60;
	if (clock_set)
		f |= PI_SET | (clock_set == 1 ? PI_NTSC : 0);
	else if (f & PI_IDNTSC)
		f |= PI_NTSC;
	clock_flags = f;
}

static unsigned short
tempo_latch(bpm)
int bpm;
{
	unsigned long e = (clock_flags & PI_NTSC ? CLOCK_NTSC : CLOCK_PAL) / 5;

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

	if (!chip)		/* no area yet: no pointer, and DMA stays off */
		return;
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

	AMIGA->dmacon = DMACLR | DMA_AUDIO;
	for (c = 0; c < 4; c++) {
		AMIGA->audio[c].loud = 0;
		ch_off[c] = PAULA_NULLOFF;
		ch_len[c] = 1;
	}
}

/* sample pointer or length of the op's channel */
static void
put_op(o)
struct paula_op *o;
{
	int c = o->ch & 3;

	if (o->op == PO_LC)
		ch_off[c] = o->off;
	else
		ch_len[c] = o->val;
	put_sample(c);
}

/*
 * One queued tick, from the level-2 interrupt.
 *
 * The ops are applied in order, except that DMA on is collected: the
 * channels a tick switches on go on together after its last op, and a
 * pointer or length written to such a channel after its PO_DMA on is held
 * until then (it is the loop part, for the next cycle).  So a tick waits
 * twice at most, however many PO_DMA it holds: SCANLINES before DMA on
 * when one of those channels was switched off in this tick (Paula must
 * see it off), and SCANLINES after (Paula must have latched pointer and
 * length before the loop part is written).  A ProTracker tick -- off,
 * registers, on, loop part -- is played as before; one PO_DMA per channel
 * costs no more than one for all four.
 */
static void
paula_tick()
{
	struct paula_tick *t;
	struct paula_op *o;
	int i, n, c, bits, off = 0, on = 0, nheld = 0;
	unsigned char held[PAULA_MAXOPS];	/* ops waiting for DMA on */

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
		case PO_LEN:
			if (on & (1 << c))
				held[nheld++] = i;
			else
				put_op(o);
			break;
		case PO_PER:
			AMIGA->audio[c].period = o->val < PERIOD_MIN ? PERIOD_MIN : o->val;
			break;
		case PO_VOL:
			AMIGA->audio[c].loud = o->val > VOLUME_MAX ? VOLUME_MAX : o->val;
			break;
		case PO_DMA:
			bits = o->val & DMA_AUDIO;
			if (o->val & PO_DMAON)
				on |= bits;
			else {
				AMIGA->dmacon = DMACLR | bits;
				off |= bits;
				on &= ~bits;
			}
			break;
		case PO_LED:
			if (o->val)
				ACIAA->pra &= ~PRA_LED;	/* power LED on: filter in */
			else
				ACIAA->pra |= PRA_LED;
			break;
		case PO_TEMPO:
			set_tempo((int)o->val);
			break;
		}
	}
	if (on && chip) {	/* no area, nothing to play: Paula stays off */
		if (off & on)		/* let Paula see the channel off */
			wait_lines(SCANLINES);
		AMIGA->dmacon = DMASET | on;
		/* the loop pointer and length that follow are for the next
		   cycle: Paula must have latched these */
		wait_lines(SCANLINES);
	}
	for (i = 0; i < nheld; i++)
		put_op(&t->op[held[i]]);
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

/* the power LED and audio filter as the open found them */
static void
restore_led()
{
	ACIAA->pra = (ACIAA->pra & ~PRA_LED) | led_was;
}

static void
stop_all()
{
	int s = spl4();

	ACIAA->icr = ICR_CLR | ICR_TB;
	ACIAA->crb = 0;
	active = 0;
	channels_off();
	restore_led();
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

	/* KM_NOSLEEP: an open fails with ENOMEM rather than wait, perhaps
	   for good, when the kernel map is short (amix kernel/dlm C3-6 v9:
	   the memory rule holds in every entry point a user's call runs) */
	ring = (struct paula_tick *)kmem_alloc(sizeof (struct paula_tick) * PAULA_RING, KM_NOSLEEP);
	if (!ring)
		return ENOMEM;
	head = tail = 0;
	played = idle = ticks = 0;
	fed = 0;
	chip = 0;
	area = 0;
	pick_clock();

	s = spl4();
	led_was = ACIAA->pra & PRA_LED;
	/* the channels are ours: no audio interrupts (Paula loops by itself) */
	AMIGA->intena = AINTCLR | AIE_AUDIO;
	AMIGA->intreq = AINTCLR | AIE_AUDIO;
	channels_off();
	/* timer B: 125 BPM until told otherwise, continuous, interrupting */
	ACIAA->crb = 0;
	set_tempo(125);
	active = 1;
	ACIAA->icr = ICR_SET | ICR_TB;
	ACIAA->crb = CRB_RUN;
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

/*
 * PAULA_SETUP, PAULA_LOAD and PAULA_PUSH run one at a time.  A process and
 * its fork share the descriptor, and LOAD and PUSH can sleep in copyin:
 * without this a second PUSH would fill the same ring slot, and a SETUP
 * would free the area a LOAD is still copying into.  The one that finds
 * another running gets EAGAIN, as a full queue gives, and tries again.
 */
static int inio;

/* the area: SETUP's work (no sleep: AllocMem and FreeMem never do) */
static int
paula_setup(size)
unsigned long size;
{
	int s;

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
	   MEMF_CLEAR or without (it clears only what it takes from kmem) */
	chip = (char *)AllocMem(size + CHIPGUARD, MEMF_CHIP);
	if (!chip)
		return ENOMEM;
	/* so clear it: the guard is the silent word, and a channel pointed
	   at a part not yet loaded plays silence.  32-bit words, then the
	   last 16 bits when the even size leaves them; nothing past the
	   request */
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
}

#define TICKHDR	((unsigned long)((struct paula_tick *)0)->op)	/* nops, pad */

/*
 * A tick from the user into ring slot t: its header, its ops, and then the
 * check.  Every op is one of PO_LC..PO_TEMPO on a channel 0..3, and a
 * PO_LC offset is even (Paula fetches words) or PAULA_NULLOFF.  The rest
 * the tick itself bounds: lengths and offsets (put_sample), period,
 * volume, tempo, DMACON bits.
 */
static int
push_tick(t, u)
struct paula_tick *t, *u;
{
	struct paula_op *o;
	int i;

	if (copyin((caddr_t)u, (caddr_t)t, TICKHDR))
		return EFAULT;
	if (t->nops > PAULA_MAXOPS)
		return EINVAL;
	if (t->nops && copyin((caddr_t)u->op, (caddr_t)t->op,
	    t->nops * sizeof (struct paula_op)))
		return EFAULT;
	for (i = 0, o = t->op; i < t->nops; i++, o++) {
		if (o->op < PO_LC || o->op > PO_TEMPO || o->ch > 3)
			return EINVAL;
		if (o->op == PO_LC && o->off != PAULA_NULLOFF && (o->off & 1))
			return EINVAL;
	}
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
	struct paula_info in;
	int s, e, next;

	switch (cmd) {
	case PAULA_SETUP:
		if (inio)
			return EAGAIN;
		inio = 1;
		e = paula_setup((unsigned long)arg);
		inio = 0;
		return e;

	case PAULA_LOAD:
		if (copyin((caddr_t)arg, (caddr_t)&ld, sizeof ld))
			return EFAULT;
		if (inio)
			return EAGAIN;
		if (!chip || ld.off > area || ld.len > area - ld.off)
			return EINVAL;
		inio = 1;
		e = copyin((caddr_t)ld.data, (caddr_t)(chip + CHIPGUARD + ld.off), ld.len) ?
		    EFAULT : 0;
		inio = 0;
		return e;

	case PAULA_PUSH:
		next = (head + 1) % PAULA_RING;
		if (next == tail || inio)
			return EAGAIN;
		inio = 1;
		e = push_tick(&ring[head], (struct paula_tick *)arg);
		inio = 0;
		if (e)
			return e;
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

	case PAULA_INFO:
		in.version = PAULA_VERSION;
		in.flags = clock_flags;
		in.clock = clock_flags & PI_NTSC ? CLOCK_NTSC : CLOCK_PAL;
		in.eclock = in.clock / 5;
		in.queue = PAULA_RING - 1;
		in.maxops = PAULA_MAXOPS;
		in.maxarea = PAULA_MAXAREA;
		if (copyout((caddr_t)&in, (caddr_t)arg, sizeof in))
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
