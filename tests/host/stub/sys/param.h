/* host test stub: PAL as the stock sys/param.h defines it */
#ifndef _STUB_PARAM_H
#define _STUB_PARAM_H
extern unsigned int displaytype;
#define PAL	(displaytype & 2)
#define NTSC	(displaytype & 1)
#define PZERO	25
#define PCATCH	0400
#endif
