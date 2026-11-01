# paula-amix: native Paula audio for AMIX

`/dev/paula` gives one program on **AMIX 2.1** (Commodore's Amiga UNIX) direct
use of Paula's four audio channels, the way a ProTracker replayer drives them
on AmigaOS. The program does not mix samples on the CPU. Instead, it queues
one batch of register changes per replayer tick, and CIA-A timer B applies
them at the song's tempo. Paula then plays the samples from chip RAM by
itself.

AMIX's own `/dev/audio` is a sample stream. A tracker on AMIX therefore had to
mix all four voices on the CPU. Measured on an A3000 with a 68060 at 80 MHz:

| Program | Through `/dev/audio` | Through `/dev/paula` |
|---|---|---|
| pt2-clone, share of the CPU spent on audio | ~41 % (float mixer), ~19 % (integer mixer) | 3–5 % |
| libmikmod beside a CPU-bound counter, work lost | ~11 % | ~5 % |
| LameCubeAMIGA42 intro with music | 28.9 fps | 30.3 fps (32.6 silent) |

The sound is Paula's own: real hardware channels, hard left/right stereo,
and the "LED" filter under program control.

## Two forms, one source

Both forms compile the same driver, `port/src/paula.c`.

| Form | Kernels | Built by |
|---|---|---|
| static | 68040/68060 kernels from [amix-040-060-port](https://github.com/asokero/amix-040-060-port) | two steps of that repository's relink chain (below) |
| module | the Amix module kernels of both lines: k30 (68030) and k46 (68040/68060), kabi minor 3 or later | `module/Makefile` with the module kit of [amix](https://github.com/amigaux/amix) (`kernel/dlm`); see `module/README.md` |

Both register character major 54. In the module form the driver loads at
the first open of `/dev/paula`; the packages `paula-k30` and `paula-k46`
carry it.

The clocks follow the machine: PAL (Paula 3 546 895 Hz, E clock 709 379 Hz)
or NTSC (3 579 545 Hz, 715 909 Hz), chosen by the PAL/NTSC bit of the
Agnus/Alice chip id. The module's `clock` parameter overrides the choice.

## Requirements (static form)

- **AMIX 2.1 on a 68040 or 68060**, with a kernel built from
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port). `/dev/paula`
  is added to such a kernel by two more steps of that repository's relink
  chain.
- A working build setup for amix-040-060-port, following its `BUILDING.md`:
  - the cross toolchain;
  - `config.sh`;
  - `AMIX_ROOT` pointing at the kernel sources of your own AMIX installation.

Developed and tested on an Amiga 3000 with a Z3660 (68060), booting the
port's kernel with the Z3660 drivers.

## Building the kernel (static form)

Copy the files into your amix-040-060-port checkout:

```sh
./install.sh /path/to/amix-040-060-port
```

There, the last two steps of the kernel's relink chain are the following. `IN`
is the kernel your chain has produced so far:

```sh
sh relink-040-audio-paula.sh IN unix-aud      # stock audio driver + the /dev/paula exclusion
sh relink-040-paula.sh unix-aud unix-paula    # /dev/paula itself (cdevsw[54])
```

What the two steps do:
- **`relink-040-audio-paula.sh`** rebuilds AMIX's audio driver from your
  `AMIX_ROOT` with two additions: `/dev/audio` and `/dev/paula` exclude each
  other, and `/dev/audio` refuses a second opener of its single stream.
  Commodore's source is copied and patched at build time; none of its text is
  in this repository. `src/patch_audio_paula.py` finds the five places it
  changes by fingerprint (line count and SHA-256), and stops if any of them
  does not match exactly once.
  - Two more patchers are optional and apply the same way, in any order:
    `src/patch_audio_safeclose.py` (a close with sound in flight stops the
    client's channels before the client is freed) and
    `src/patch_audio_initwait.py` (the open's wait for Paula's first
    interrupt is bounded; a silent Paula fails the open with `EIO`). To use
    them, patch a copy of `audio.c` and `audio.h` with them and point
    `AUDIO_SRC` at it.
- **`relink-040-paula.sh`** compiles `src/paula.c` and links it in. It replaces
  the level-2 CIA-A handler with one that also serves timer B, and registers
  the driver as character major 54, the major of the module form.
  - `src/patch_paula_cdevsw.py` finds slot 54 from the kernel's `cdevsw` and
    `cdevcnt`, and refuses a table whose entries are not 52 bytes, one too
    short, or a slot 54 already in use.
  - Both steps check their input and output: symbols, unresolved references,
    `.data` alignment, and 060-safe instructions.

Then finish the kernel as usual: root/swap stamp, CACR, install in `/stand`.

## The device node

`/dev/paula` is `c 54 0`, mode 0660, owner `root`, group `audio`. Whoever
holds it owns the chip-RAM sample area (up to 1.5 MB) and CIA-A timer B, so
it is not open to every user. The packages ship the node. For the static
form, once, as root: an `audio` group in `/etc/group` with the users who may
play (`audio::<gid>:<user>,<user>`), then

```sh
mknod /dev/paula c 54 0 && chgrp audio /dev/paula && chmod 660 /dev/paula
```

## Testing

`port/test-tools/paulatest.c` is compiled on AMIX itself with the native `cc`:

```sh
cc -O -o paulatest paulatest.c && ./paulatest
```

It checks three things:
- that `/dev/audio` is refused while `/dev/paula` is open;
- the tick rate (50 per second at 125 BPM);
- that the queue never runs dry.

It also plays a scale: a square wave on channel 0 and a saw an octave lower on
channel 1. `paulatest -n` checks the refusals (bounds, malformed ticks, a
full queue) and `PAULA_WAIT`, `PAULA_RESET` and open/close/open;
`paulatest -d [seconds]` keeps the queue full and compares the ticks played
with the clock; `paulatest -i` prints `PAULA_INFO`; `paulatest -s
[seconds]` plays silent ticks of 24 DMA off/on pairs at 255 BPM, the
worst case for the waits.

On the build host, `make -C tests` runs the host tests: the driver in both
forms against a fake of Paula, CIA-A and the kernel, the `cdevsw` patcher
on synthetic kernels, `install.sh`, and the three audio patchers on the
stock `audio.c` (from [amix-system](https://github.com/amigaux/amix-system)
beside this repository, or `AUDIO_C`).

## Programming interface

`include/paula.h` is the complete interface (the same file as `port/src/paula.h`).

| ioctl | Argument | |
|---|---|---|
| `PAULA_SETUP` | sample area size in bytes, passed as the value | Allocates the sample area in chip RAM (up to 1.5 MB). |
| `PAULA_LOAD` | `struct paula_load *` | Copies sample data to an offset in the area. |
| `PAULA_PUSH` | `struct paula_tick *` | Queues one tick of operations. Returns `EAGAIN` when the queue (63 ticks) is full. |
| `PAULA_STATUS` | `struct paula_status *` | Ticks played, underruns (ticks that found the queue empty after a push), ticks queued, area size. |
| `PAULA_RESET` | – | Drops the queue and stops the channels. |
| `PAULA_INFO` | `struct paula_info *` | Interface version (2), PAL or NTSC and why, Paula's clock and the E clock in Hz, the queue, operation and area limits. A version 1 driver answers `EINVAL`. |
| `PAULA_WAIT` | the number of ticks, passed as the value | Sleeps until no more than that many ticks are queued; `EINTR` on a signal. Version 2. |

A tick holds up to 48 operations, applied in order:

| Operation | Meaning |
|---|---|
| `PO_LC` | Sample start (an even offset in the area). `PAULA_NULLOFF` selects a silent word. |
| `PO_LEN` | Length in words. |
| `PO_PER` | Period. |
| `PO_VOL` | Volume, 0–64. |
| `PO_DMA` | DMACON bits 0–3; bit 15 (`PO_DMAON`) set switches on, clear switches off. |
| `PO_LED` | The "LED" low-pass filter. |
| `PO_TEMPO` | BPM, 32–255: the timer B rate from the next tick on. |

Within a tick the ProTracker order is kept: DMA off, registers, a short wait,
DMA on, a short wait, then the loop pointers. The channels a tick switches
on go on together after its last operation, and a pointer or length written
to such a channel after its `PO_DMA` on waits until Paula has latched the
first. So a tick waits twice at most (5 scan lines each, about 0.64 ms in
all), however many `PO_DMA` it holds. A player keeps a few ticks queued
ahead and waits with `PAULA_WAIT`, or for `PAULA_STATUS.played` to advance.
Closing the device stops the channels, puts the "LED" filter back as the
open found it and frees the area.

`PAULA_PUSH` refuses (`EINVAL`) a tick with more than 48 operations, an
unknown operation, a channel above 3 or an odd sample offset; nothing of it
is queued. `PAULA_SETUP`, `PAULA_LOAD` and `PAULA_PUSH` run one at a time: a
second one while another is copying (from a forked process sharing the
descriptor) gets `EAGAIN`.

Only one program can hold `/dev/paula` at a time, and never while `/dev/audio`
is open. A program can fall back to `/dev/audio` when it gets `EBUSY`.

## Status

- **The static form on hardware, 2026-10-27:** the driver in the original
  author's kernel tree, on an A3000 with a Z3660 68060.
  `docs/REALHW-PAULA-261027.md` records the measurements (49.8 ticks a
  second at 125 BPM, no underrun). Players using it there: a pt2-clone port,
  a libmikmod output driver (with an automatic fallback to `/dev/audio` for
  modules with more than four channels), and ioquake3's sound output. That
  tree's audio driver also carried a safe close with sound in flight and
  sample-locked stereo.
- **The static form as this repository builds it** (the stock audio driver
  with the exclusion, and the optional safe close and bounded init wait):
  builds and links; not run on hardware.
- **The module form in emulation, 2026-10-28:** on the k30 and k46 module
  kernels under amiberry (an A4000 with a Z3660), `paulatest` played at 49.6
  to 50.0 ticks a second, `/dev/audio` was refused while `/dev/paula` was
  open and the other way round, and after a `/dev/audio` player was killed
  mid-stream both devices opened again. Not run on hardware.
- **The changes of 2026-11-01** (the bounded DMA waits, the PAL/NTSC
  choice, `PAULA_INFO`, `PAULA_WAIT`, the tick checks, the open's
  self-test) pass the host tests; not run on a box yet.

## Licence

MIT (`LICENSE`): isoriano1968's driver, and the changes and additions of
Jussi Alanärä, each file's header saying which. AMIX itself is not free
software, and nothing from it is in this repository.

## Credits

- isoriano1968, for the driver, its interface and the static relink path
  (paula-amix).
- asokero, for amix-040-060-port, which this builds on.
- jusii, for the Z3660 AMIX drivers, the module form and the audio driver
  patches.
