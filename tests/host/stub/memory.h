/* host test stub: AllocMem as the stock kernel has it */
#define MEMF_CHIP	2
#define MEMF_CLEAR	0x10000
extern char *AllocMem();
extern void FreeMem();
