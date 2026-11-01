/*
 * drv_static.c -- the static driver, port/src/paula.c, with accessors.
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
#include "paula.c"

extern struct acia fake_acia;

int drv_open() { dev_t d = 0; return paulaopen(&d, 3, 0, (struct cred *)0); }
int drv_close() { return paulaclose((dev_t)0, 3, 0, (struct cred *)0); }
int drv_ioctl(cmd, arg) int cmd; long arg;
{ int rv; return paulaioctl((dev_t)0, cmd, (int)arg, 3, (struct cred *)0, &rv); }
void drv_timer() { fake_acia.icr = ICR_TB; aciaaintr((pcb_t *)0); }
int drv_busy() { return busy; }
int drv_active() { return active; }
unsigned long drv_ticks() { return ticks; }
char *drv_chip() { return chip; }
int drv_module() { return 0; }
int drv_load(p) char *p; { return 0; }
int drv_unload() { return 0; }
void drv_halt() {}
