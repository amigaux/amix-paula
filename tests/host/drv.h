/*
 * drv.h -- what the host tests reach in the driver: its entry points and,
 * through drv_*.c, its static state.  One test program per form: the
 * static driver (port/src/paula.c) and the module (module/paula_mod.c).
 *
 * Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
 */
int drv_open(void);		/* the form's d_open */
int drv_close(void);		/* the form's d_close */
int drv_ioctl(int cmd, long arg);
void drv_timer(void);		/* one timer B underflow, as the form takes it */
int drv_busy(void);
int drv_active(void);
unsigned long drv_ticks(void);
char *drv_chip(void);
int drv_module(void);
int drv_load(char *params);	/* the module's _load (0 for the static form) */
int drv_unload(void);
void drv_halt(void);
