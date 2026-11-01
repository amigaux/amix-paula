# /dev/paula as a loadable module

The same driver as the static relink path (`port/src/paula.c`), built as a
loadable kernel module for the Amix module system (the ABI kit in
[amix](https://github.com/amigaux/amix) under `kernel/dlm`, contracts C3-1,
C3-2 and C3-6). One module per kernel line: `k30` (68030)
and `k46` (68040/68060). The static path (`install.sh`, `port/relink-040-*.sh`)
is unchanged and still builds the same `paula_040.o` from the same file.

## Layout

| Path | Content |
|---|---|
| `paula_mod.c` | includes `port/src/paula.c` with `PAULA_MODULE` defined, and adds the module glue: the level-2 handler, open/close with the self-test, `_load`, `_unload`, `_halt`, the `cdevsw` row |
| `keys` | the `.moddata` keys, one `key=value` per line; the `cdevsw` major is taken from here |
| `Makefile` | builds both lines on the kit's `mk/module.mk`, each in `build/<line>/stage` with the kit's `Master` generated there from `keys` |
| `VERSION` | the package version, `x.y.z-rev`, the same on both lines |

## Building

You need the pinned gcc-cross-amix (`m68k-cbm-sysv4-*`) on `PATH`, `python3`,
and the built kits of an amix checkout (`make kit` in its `kernel/dlm`).

```sh
make -C module            # both lines: build/k30/mod.d/paula, build/k46/mod.d/paula
make -C module k46        # one line
```

`DLM` names the kit's directory, `kernel/dlm` of the amix checkout
(default `../amix/kernel/dlm`, amix cloned beside this repo); `KIT_k30` and
`KIT_k46` name kits directly. Each line builds in its staging directory
(`build/<line>/stage`), which holds copies of `paula_mod.c`, `paula.c`,
`paula.h`, the kit's Amiga `memory.h` (as in the static relink pass, so
`#include "memory.h"` cannot take libc's) and the `Master` generated from
`keys`; nothing is written outside `build/`. After the kit's `modfix` and
`modcheck.py` it checks that the object still writes SR (`spl4`/`splx` not
dropped by `-traditional`, with the kit's objdump) and that the two copies
of `paula.h` are the same bytes.

## What the module needs from the kernel (not in DDI v1.2)

Today's kits (kabi 1.2) refuse the module. `modfix` names five symbols:

| Name | Kind | Provider | Why |
|---|---|---|---|
| `AllocMem`, `FreeMem` | func | stock (`amiga/console/memory.c`) | the sample area must be chip RAM; no DDI call gives chip memory (`dma_alloc` never does, C4-5 §5.4) |
| `displaytype` | data, read-only | stock (`amiga/driver/cl.c`) | `PAL` (`sys/param.h`): the E clock behind the tempo, 709 379 or 715 909 Hz |
| `paula_busy` | data, written by this module only | the audio driver patched by `port/src/patch_audio_paula.py` | `/dev/audio` refuses its open while it is set |
| `audio_nopen` | data, read-only | the same | `/dev/paula` refuses its open while `/dev/audio` has a client |

And the major: **c 54**, paula's in the registry and free in the stock
tables; the static form registers the same major (53 is `kinfo`'s).

With those five names as kabi minor 3 and `c 54 paula` in the registry,
both lines pass `modfix` and `modcheck.py` (k46 with the 68060 scan and the
page-geometry check).

## The timer: one-shot, restarted from the level-2 chain

The static form replaces `aciaaintr`, the level-2 CIA-A handler, with one
that also serves timer B. A module may not; it attaches with
`intr_attach(2, …)`, and the chain runs after the stock handlers. By then
`aciaaintr` has read CIA-A's ICR, which clears every pending bit, timer B's
included. The stock handler ignores that bit, and neither kernel line uses
CIA-A timer B (read from the stock `acia.c` and `cl.c`, the port's `src/`,
the module core in amix's `kernel/dlm`).

So the module runs timer B one-shot. An underflow stops the timer and clears
CRB's START bit. `paula_int2` restarts the timer from the latch first, then
applies one queued tick. It claims only when the driver is active and START is
clear. The clock (CIA-A timer A, also level 2) calls the chain every clock
tick, so even a missed timer interrupt is caught within one clock tick.

The cost: each period is longer than a continuous timer's by the time from
the underflow to the restart, typically tens of microseconds against a 20 ms
period at 125 BPM. That is a slowdown of about 0.1 %, measured by the Bench
row below. A drift-free form would need the core to read CIA-A's ICR once and
hand the timer B bit to a hook (C3-6 has no such hook today).

`PO_TEMPO` writes the new period into `tb_latch`, and the next restart loads
it. The tempo therefore changes from the next tick on, as in the static form.
In one-shot mode a write to the timer's high byte would restart a running
timer, so the module never writes it outside the restart.

## Open, close and the module entries

- **open**: refused (`EBUSY`) while `/dev/paula` or `/dev/audio` is open.
  A running timer B is not a refusal: Kickstart's timer.device (3.0 to
  3.2.x) starts CIA-A timer B continuous with latch `$FFFF` as its E-clock
  counter, the kernel never stops it, so it runs at every first open; the
  open stops and reprograms it, as the static form does. The handler is
  attached before the timer starts. The open then waits, in one-tick sleeps of at most 25 ticks,
  for the first tick to arrive (the self-test of C3-6 §1.3). With no tick it
  undoes everything and returns `EIO`.
- **close**: as the static form (channels off, area and queue freed), then
  the handler detached.
- **`_load`**: nothing; the module is `demand` class and loads at the first
  open of `/dev/paula`.
- **`_unload`**: `unload=allowed`. The core never calls it while the device is
  open. It detaches the handler if it is still attached.
- **`_halt`**: if the device is open, timer B off and audio DMA off. No sleep.

The module allocates the tick queue (24 832 bytes, `kmem_alloc`) at open and
the sample area (chip, up to 1.5 MB, `AllocMem`) at `PAULA_SETUP`. The image
is 2 820 bytes.

## k30 and k46

The hardware is the same: Paula, CIA-A and chip RAM below 2 MB, identity
mapped on both lines (the stock audio driver hands `AllocMem` chip memory to
Paula the same way). The source is the same as well. What differs:

- the kit: `-m68020` with cpu 68030 on k30, `-m68040` with cpu 68040 on k46;
  the 68060 scan and page geometry check are k46's (the k30 object also scans
  clean);
- the static form exists only on k46 (the port's relink chain). On k30 the
  module is the only form;
- a slower CPU on k30 means a longer interrupt latency, and so a little more
  one-shot slowdown.

## The /dev/audio exclusion in a module kernel

The kernel cut rebuilds the stock audio driver with
`port/src/patch_audio_paula.py`, which defines `paula_busy` and
`audio_nopen`, with `port/src/patch_audio_safeclose.py`, and with
`port/src/patch_audio_initwait.py`. All three are applied by fingerprint to a
build-time copy of the stock `audio.c`, in any order.

Neither open sleeps between testing the other's flag and setting its own
(C3-2 §3): `paulaopen()` sets `paula_busy` before its `kmem_alloc`, and the
patched `audioopen()` tests `paula_busy` again at `splaudio` after its own
allocation, before `audioinit()` or the client list is touched.

The safe close fixes a stock hazard. `audioclose()` frees the client but
leaves it in `audio_channel[].client`. After a mid-stream kill, `audiointr()`
then reads the freed client (`acl->repeat`, `acl->q`) and the channel can stay
claimed. With the patch, the close stops each channel the client holds, at
`splaudio`, before freeing it.

The bounded init wait fixes another. On the first open of a boot,
`audioinit()` hand-feeds a word to each channel and busy-waits at `splaudio`,
with no limit, for the channel's interrupt request; a Paula that never raises
it (amiberry `sound_output=none`, or a sick chip) stops the box there. With
the patch each channel gets 32 scan lines (about 2 ms, counted on the beam
counter; no CIA timer is touched), and on a timeout the open frees its client
and fails with `EIO`. The next open tries again.

On k46, `relink-040-audio-paula.sh` does the build as it does for the static
path; `AUDIO_SRC` points it at a copy already patched with
`patch_audio_safeclose.py` and `patch_audio_initwait.py`.

## Packages

One package per line, made by amix-packages (as monsw's):

| | k30 | k46 |
|---|---|---|
| catalog name | `paula-k30` | `paula-k46` |
| pkginst | `paula30` | `paula46` |

Each ships `/etc/conf/mod.d/paula`, `/etc/conf/modreg.d/paula` (the
package's parameter defaults), `/dev/paula` as `c 54 0` mode 0660 owner
`root` group `audio`, and `include/paula.h` for programs. It needs a
kernel of kabi minor 3, and the `audio` group on the box: its members may
play. The holder of `/dev/paula` owns up to `maxarea` bytes of chip RAM
and CIA-A timer B, so the node is not open to every user.

## Testing

- **Host:** `make -C module` against the kits (above). The static path's own
  checks: `install.sh` into a port checkout, then the two relink steps.
- **Bench (emulator first):** `port/test-tools/paulatest.c` on the module
  kernel, with `/dev/paula` made as `c 54 0`, mode 0660, group `audio`. It shows `/dev/audio` refused
  while `/dev/paula` is open, the tick rate (50.0 per second expected at
  125 BPM, less the one-shot slowdown) and idle 1 or 2: the open's
  self-test waits for a tick while the queue is empty (always 1), and the
  program's setup before its first push can span one more. paulatest's
  rate window also holds the wait for the first tick and its 50 ms drain
  poll, up to about 2 % at 4 s; a drift measurement compares `played`
  over a long window with the queue kept full.
- **Emulation and `/dev/audio`:** the stock `audioinit()` waits at `splaudio`,
  with no timeout, for each channel's hand-fed word interrupt. Amiberry with
  `sound_output=none` emulates no Paula interrupts, so on a kernel without
  `patch_audio_initwait.py` the first `/dev/audio` open of a boot freezes the
  box there (network, clock and keyboard stop). With it, that open fails with
  `EIO` after about 2 ms and the box carries on. Bench rows that play through
  `/dev/audio` need `sound_output=interrupts` or better. This is independent
  of `/dev/paula`.
