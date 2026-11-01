/*
 * fake.h -- the host model of the hardware and kernel that port/src/paula.c
 * and module/paula_mod.c run against in the host tests (tests/host).
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
#ifndef _FAKE_H
#define _FAKE_H

#define UNITS_PER_LINE	8	/* hw_step() calls per scan line */
#define DMACON_IDLE	0x7fff	/* never written by the driver: "no write" */

enum { EV_DMACON = 1, EV_LC };
struct ev {
	long	t;		/* fake_now when the write was seen */
	int	kind;		/* EV_DMACON, EV_LC */
	unsigned v;		/* DMACON value; LC: offset from the chip area */
	int	ch;
};

extern long fake_now;
extern struct ev fake_ev[];
extern int fake_nev;
extern int fake_ipl;
extern int paula_busy, audio_nopen;
extern unsigned int displaytype;

/* the kernel's side */
extern int fake_kmem_flag, fake_kmem_fail, fake_kmem_live;
extern int fake_chip_live, fake_chip_fail;
extern int fake_race;		/* timeout() below spl4 fires at once */
extern int fake_hung;		/* a sleep that nothing would have ended */
extern int fake_sleeps, fake_signal;
extern int (*fake_int2)();	/* the attached level-2 handler */
extern void (*fake_on_tick)();	/* one clock tick passes in a sleep */
extern void (*fake_on_attach)();	/* intr_attach may sleep: another open runs */
extern void (*fake_on_copyin)();	/* copyin may sleep: another call runs */
extern int fake_printfs;
extern unsigned char *fake_chip_base;

void fake_reset(void);
long fake_line(void);
int fake_chip_overrun(void);	/* bytes written past the last AllocMem */
void hw_step(void);

#endif
