/* host test stub: the interrupt level is a variable */
#ifndef _STUB_INLINE_H
#define _STUB_INLINE_H
extern int fake_ipl;
static int spl4(void) { int s = fake_ipl; if (fake_ipl < 4) fake_ipl = 4; return s; }
static int spl0(void) { int s = fake_ipl; fake_ipl = 0; return s; }
static void splx(int s) { fake_ipl = s; }
#endif
