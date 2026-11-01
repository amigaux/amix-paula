/*
 * paulatest.c -- first light for /dev/paula (src/paula.c): plays a short
 * scale on channels 0 and 1 from a looped 32-byte waveform, through the
 * driver's tick queue, and checks the tick rate and the exclusion with
 * /dev/audio.
 *
 *   paulatest            needs /dev/paula (mknod /dev/paula c 54 0)
 *   paulatest -n         the refusals: bounds, bad ticks, a full queue,
 *                        RESET, open/close/open (interface version 2)
 *   paulatest -d [s]     drift: the queue kept full for s seconds (30),
 *                        ticks played against the clock
 *   paulatest -i         what the driver reports (PAULA_INFO)
 *
 * Expected: about 50 ticks a second at 125 BPM (CIA-A timer B), "idle"
 * small (the queue is kept full), /dev/audio refused while open.
 * Built with the native cc on AMIX: cc -o paulatest paulatest.c
 *
 * Copyright (c) 2026 isoriano1968; -n, -d, -i (c) 2026 Jussi Alanärä.
 * MIT licence, see LICENSE.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <poll.h>
#include "paula.h"

static long now_ms()
{
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *)0);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static int fd;

static void op(t, o, c, v, off)
struct paula_tick *t;
int o, c, v;
unsigned long off;
{
	struct paula_op *p = &t->op[t->nops++];

	p->op = o;
	p->ch = c;
	p->val = v;
	p->off = off;
}

static void push(t)
struct paula_tick *t;
{
	while (ioctl(fd, PAULA_PUSH, (char *)t) < 0) {
		if (errno != EAGAIN) {
			perror("PAULA_PUSH");
			exit(1);
		}
		poll((struct pollfd *)0, 0, 10);	/* the queue is full */
	}
}

static int fails;

/* one refusal: the call must fail with errno e */
static void expect(what, rc, e)
char *what;
int rc, e;
{
	int ok = rc < 0 && errno == e;

	printf("  %-44s %s\n", what, ok ? "refused (right)" : "WRONG");
	if (!ok)
		fails++;
}

static void check(what, ok)
char *what;
int ok;
{
	printf("  %-44s %s\n", what, ok ? "ok" : "WRONG");
	if (!ok)
		fails++;
}

static int queued()
{
	struct paula_status st;

	ioctl(fd, PAULA_STATUS, (char *)&st);
	return (int)st.queued;
}

static int info(in)
struct paula_info *in;
{
	if (ioctl(fd, PAULA_INFO, (char *)in) < 0) {
		printf("PAULA_INFO: %s (a version 1 driver)\n", errno == EINVAL ? "EINVAL" : "failed");
		return -1;
	}
	printf("version %lu, %s clocks (Paula %lu Hz, E clock %lu Hz)%s%s%s\n",
	    in->version, in->flags & PI_NTSC ? "NTSC" : "PAL", in->clock, in->eclock,
	    in->flags & PI_SET ? ", set by the clock parameter" : "",
	    in->flags & PI_IDNTSC ? ", chip id NTSC" : ", chip id PAL",
	    in->flags & PI_FIELD60 ? ", 60 Hz fields" : ", 50 Hz fields");
	printf("queue %lu ticks, %lu operations a tick, area up to %lu bytes\n",
	    in->queue, in->maxops, in->maxarea);
	return 0;
}

/* -n: what the driver must refuse, and what must still work after */
static int negative()
{
	struct paula_tick t;
	struct paula_load ld;
	struct paula_info in;
	char buf[64];
	int i, n, rc;

	if (info(&in) < 0)
		return 1;
	expect("PAULA_SETUP above the limit: EINVAL",
	    ioctl(fd, PAULA_SETUP, (char *)(in.maxarea + 2)), EINVAL);
	ld.off = 0;
	ld.len = sizeof buf;
	ld.data = buf;
	expect("PAULA_LOAD before PAULA_SETUP: EINVAL", ioctl(fd, PAULA_LOAD, (char *)&ld), EINVAL);
	check("PAULA_SETUP 1024", ioctl(fd, PAULA_SETUP, (char *)1024) == 0);
	ld.off = 1000;
	expect("PAULA_LOAD past the area: EINVAL", ioctl(fd, PAULA_LOAD, (char *)&ld), EINVAL);
	ld.off = 0xfffffff0UL;
	expect("PAULA_LOAD, offset wrapping: EINVAL", ioctl(fd, PAULA_LOAD, (char *)&ld), EINVAL);
	t.nops = PAULA_MAXOPS + 1;
	expect("PAULA_PUSH, 49 operations: EINVAL", ioctl(fd, PAULA_PUSH, (char *)&t), EINVAL);
	t.nops = 0;
	op(&t, 8, 0, 0, 0L);
	expect("PAULA_PUSH, no such operation: EINVAL", ioctl(fd, PAULA_PUSH, (char *)&t), EINVAL);
	t.nops = 0;
	op(&t, PO_VOL, 4, 10, 0L);
	expect("PAULA_PUSH, channel 4: EINVAL", ioctl(fd, PAULA_PUSH, (char *)&t), EINVAL);
	t.nops = 0;
	op(&t, PO_LC, 0, 0, 33L);
	expect("PAULA_PUSH, odd sample offset: EINVAL", ioctl(fd, PAULA_PUSH, (char *)&t), EINVAL);
	check("nothing queued by the refusals", queued() == 0);
	t.nops = 0;
	op(&t, PO_VOL, 0, 0, 0L);
	for (i = 0; i < 1000 && (rc = ioctl(fd, PAULA_PUSH, (char *)&t)) == 0; i++)
		;
	n = queued();
	expect("PAULA_PUSH on a full queue: EAGAIN", rc, EAGAIN);
	check("the full queue holds PAULA_INFO's queue", n >= (int)in.queue - 1 && n <= (int)in.queue);
	check("PAULA_WAIT down to 10 ticks", ioctl(fd, PAULA_WAIT, (char *)10) == 0 && queued() <= 10);
	check("PAULA_RESET empties the queue", ioctl(fd, PAULA_RESET, (char *)0) == 0 && queued() == 0);
	check("PAULA_SETUP 0 frees the area", ioctl(fd, PAULA_SETUP, (char *)0) == 0);
	close(fd);
	for (i = 0; i < 3; i++) {
		fd = open("/dev/paula", O_RDWR);
		check("open again after close", fd >= 0);
		if (fd < 0)
			return 1;
		if (i < 2)
			close(fd);
	}
	printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
	return fails != 0;
}

/* -d: the queue kept full for secs seconds; ticks played against the clock */
static int drift(secs)
int secs;
{
	struct paula_tick t;
	struct paula_status st;
	struct paula_info in;
	long t0, t1;
	unsigned long p0;
	double want, got;

	if (info(&in) < 0)
		in.eclock = 709379L;
	t.nops = 0;
	op(&t, PO_TEMPO, 0, 125, 0L);
	push(&t);
	t.nops = 0;
	while (queued() < 20)
		push(&t);
	ioctl(fd, PAULA_STATUS, (char *)&st);
	p0 = st.played;
	t0 = now_ms();
	do {
		push(&t);			/* blocks on EAGAIN: the queue stays full */
		t1 = now_ms();
	} while (t1 - t0 < secs * 1000L);
	ioctl(fd, PAULA_STATUS, (char *)&st);
	t1 = now_ms();
	/* timer B counts eclock * 2.5 / 125 E-clock ticks, rounded down */
	want = (double)in.eclock / (double)(in.eclock * 5 / 2 / 125);
	got = (st.played - p0) * 1000.0 / (t1 - t0);
	printf("drift: %lu ticks in %ld ms: %.3f per second, expect %.3f (%+.0f ppm); idle %lu\n",
	    st.played - p0, t1 - t0, got, want, (got - want) / want * 1e6, st.idle);
	return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
	static short notes[] = { 428, 381, 339, 320, 285, 254, 226, 214 };	/* C-2 .. C-3 */
	struct paula_tick t;
	struct paula_load ld;
	struct paula_status st;
	struct paula_info in;
	char wave[64];
	long t0, t1;
	int i, n, a;
	unsigned long played0;

	fd = open("/dev/paula", O_RDWR);
	if (fd < 0) {
		perror("/dev/paula");
		return 1;
	}
	if (argc > 1 && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'n':
			return negative();
		case 'd':
			return drift(argc > 2 ? atoi(argv[2]) : 30);
		case 'i':
			return info(&in) < 0;
		}
		fprintf(stderr, "usage: paulatest [-n | -d [seconds] | -i]\n");
		return 2;
	}
	a = open("/dev/audio", O_WRONLY);
	printf("/dev/audio while /dev/paula is open: %s\n",
	    a < 0 ? (errno == EBUSY ? "refused, EBUSY (right)" : "refused, other error") : "OPENED (wrong)");
	if (a >= 0)
		close(a);

	/* a soft square in the first 32 bytes, a saw in the next 32 */
	for (i = 0; i < 32; i++) {
		wave[i] = i < 16 ? 90 : -90;
		wave[32 + i] = (char)(i * 8 - 128);
	}
	if (ioctl(fd, PAULA_SETUP, (char *)1024) < 0) {
		perror("PAULA_SETUP");
		return 1;
	}
	ld.off = 0;
	ld.len = sizeof wave;
	ld.data = wave;
	if (ioctl(fd, PAULA_LOAD, (char *)&ld) < 0) {
		perror("PAULA_LOAD");
		return 1;
	}

	ioctl(fd, PAULA_STATUS, (char *)&st);
	played0 = st.played;
	t0 = now_ms();
	for (n = 0; n < 8 * 25; n++) {		/* 8 notes of 25 ticks, 4 s at 125 BPM */
		t.nops = 0;
		if (n == 0)
			op(&t, PO_TEMPO, 0, 125, 0L);
		if (n % 25 == 0) {
			int k = n / 25;
			/* ProTracker's order: off, set, on, then the loop part */
			op(&t, PO_DMA, 0, 0x0003, 0L);
			op(&t, PO_LEN, 0, 16, 0L);
			op(&t, PO_LC, 0, 0, 0L);
			op(&t, PO_PER, 0, notes[k], 0L);
			op(&t, PO_VOL, 0, 48, 0L);
			op(&t, PO_LEN, 1, 16, 0L);
			op(&t, PO_LC, 1, 0, 32L);
			op(&t, PO_PER, 1, notes[k] * 2, 0L);
			op(&t, PO_VOL, 1, 32, 0L);
			op(&t, PO_DMA, 0, 0x8003, 0L);
			op(&t, PO_LC, 0, 0, 0L);
			op(&t, PO_LEN, 0, 16, 0L);
			op(&t, PO_LC, 1, 0, 32L);
			op(&t, PO_LEN, 1, 16, 0L);
		}
		push(&t);
	}
	/* let the queue play out */
	do {
		poll((struct pollfd *)0, 0, 50);
		ioctl(fd, PAULA_STATUS, (char *)&st);
	} while (st.queued > 0 && now_ms() - t0 < 10000);
	t1 = now_ms();
	printf("played %lu ticks in %ld ms: %.1f per second (expect 50.0); idle %lu, area %lu\n",
	    st.played - played0, t1 - t0, (st.played - played0) * 1000.0 / (t1 - t0), st.idle, st.area);
	close(fd);
	return 0;
}
