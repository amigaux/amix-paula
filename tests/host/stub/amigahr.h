/*
 * host test stub: the custom chips and CIA-A as plain memory.  Every use of
 * AMIGA or ACIAA first calls hw_step(), which moves the beam on and logs the
 * writes made since the last call.
 */
#ifndef _STUB_AMIGAHR_H
#define _STUB_AMIGAHR_H
struct amiga_audio {
	signed char	*buf;
	unsigned short	size, period, loud, data;
};
struct amiga {
	unsigned short	vposr, vhposr, dmacon, intena, intreq;
	struct amiga_audio audio[4];
};
struct acia {
	unsigned char	pra, ddra, talo, tahi, tblo, tbhi, icr, cra, crb;
};
extern struct amiga fake_amiga;
extern struct acia fake_acia;
extern void hw_step();
#define AMIGA	(hw_step(), &fake_amiga)
#define ACIAA	(hw_step(), &fake_acia)

#define AINTSET	0x8000
#define AINTCLR	0x0000
#define AIEAUD3	0x0400
#define AIEAUD2	0x0200
#define AIEAUD1	0x0100
#define AIEAUD0	0x0080
#define DMASET	0x8000
#define DMACLR	0x0000
#define DMAAUD3	0x0008
#define DMAAUD2	0x0004
#define DMAAUD1	0x0002
#define DMAAUD0	0x0001
#define ICR_SET	0x80
#define ICR_CLR	0x00
#define ICR_TA	0x01
#define ICR_TB	0x02
#define ICR_SP	0x08
#define ICR_FLG	0x10
#endif
