/* host test stub: the driver row and the wrapper, reduced to what a test calls */
struct mod_drv_data {
	int	pad[1];
	int	a, b;
	struct { int (*open)(), (*close)(), (*read)(), (*write)(), (*ioctl)(); } sw;
	int	major, count;
};
#define MOD_DRV_WRAPPER(p, load, unload, halt, desc) \
	int (*p##_t_load)() = load; int (*p##_t_unload)() = unload; void (*p##_t_halt)() = halt;
