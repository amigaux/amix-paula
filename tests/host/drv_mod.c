/*
 * drv_mod.c -- the module, module/paula_mod.c (which includes paula.c),
 * with accessors.
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
#include "paula_mod.c"

extern struct acia fake_acia;
extern int (*fake_int2)();

int drv_open() { dev_t d = 0; return paula_mopen(&d, 3, 0, (struct cred *)0); }
int drv_close() { return paula_mclose((dev_t)0, 3, 0, (struct cred *)0); }
int drv_ioctl(cmd, arg) int cmd; long arg;
{ int rv; return paulaioctl((dev_t)0, cmd, (int)arg, 3, (struct cred *)0, &rv); }
/* one-shot: the underflow stops the timer; the level-2 chain runs */
void drv_timer() { fake_acia.crb &= ~CRB_START; if (fake_int2) fake_int2((char *)0); }
int drv_busy() { return busy; }
int drv_active() { return active; }
unsigned long drv_ticks() { return ticks; }
char *drv_chip() { return chip; }
int drv_module() { return 1; }
int drv_load(p) char *p; { return paula_t_load(p); }
int drv_unload() { return paula_t_unload(); }
void drv_halt() { paula_t_halt(); }
