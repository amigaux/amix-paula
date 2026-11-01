#!/usr/bin/env python3
"""patch_audio_initwait.py -- /dev/audio's open gives up on a silent Paula.
Applied to a build-time copy of the stock AMIX 2.1c audio driver
(usr/sys/amiga/driver/audio.c), before or after patch_audio_paula.py and
patch_audio_safeclose.py, in any order.  Usage: patch_audio_initwait.py audio.c

audioopen() calls audioinit() at splaudio (spl7, which the AMIX inline.h
maps to IPL 4).  On the first open of a boot audioinit() hand-feeds one word
to each channel and busy-waits for the channel's audio interrupt request,
with no limit.  A Paula that never raises
it (an emulator with no sound emulation, amiberry sound_output=none, or a
sick chip) holds the box there for good: clock, network and keyboard stop.

This bounds the wait.  The time base is the beam counter: every change of the
line byte of VHPOSR is one scan line (64 us PAL, 63.5 us NTSC), and each
channel gets AUDIO_INITLINES of them, about 2 ms.  A read count ends the loop
as well if the counter itself never moves; it is a backstop, not a clock.
(No CIA timer is touched: /dev/paula owns timer B.)  On a timeout the channel
is left as audioinit() set it up (its interrupt and DMA off), its request is
cleared, audioinit() returns without marking itself done, and audioopen()
fails with EIO.  At that point the open has set up nothing but its client
allocation and splaudio -- the client list, audio_nopen and q_ptr come after
the call, and paula_busy is /dev/paula's -- so it frees the one, leaves the
other and returns.  The next open tries again.  audioopen() is audioinit()'s
only caller (the stock kernel has one relocation against it, in audioopen).

Commodore's source is not in this repository, and neither is any of its
text.  Each place is found as patch_audio_paula.py finds its places: the
number of lines and the SHA-256 of those lines exactly as the stock file has
them, which must match exactly once; our lines go in after it.  None of the
places is one the other two patches use, so the result does not depend on
the order.

  1. the declaration of the silent buffer      -> the bounded wait, the flag
  2. audioinit(): before the busy-wait          -> the wait, and the way out
  3. audioopen(): the call of audioinit()       -> EIO when it timed out

Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
"""
import hashlib
import sys

MARK = "audio_waitdone"
PLACES = [
    # (what, lines, sha256 of those lines, our lines to insert after them)
    ("the silent buffer declaration", 1,
     "0566248a3572dcc497f121a1770168249347ea1399fc6d05870b458524d338fa",
     ["",
      "/*",
      " * Bounded wait for a hand-fed word (amix-paula, patch_audio_initwait.py).",
      " * Time is counted in scan lines: each change of VHPOSR's line byte is one.",
      " * AUDIO_INITREADS only ends the loop if the beam counter never moves.",
      " */",
      "#define AUDIO_INITLINES\t32\t\t/* about 2 ms */",
      "#define AUDIO_INITREADS\t0x400000",
      "static int audio_initerr;\t/* audioinit() timed out: audioopen() fails */",
      "",
      "static int",
      "audio_waitdone(int bit)",
      "{",
      "    unsigned short line, now;",
      "    int lines = 0, reads = 0;",
      "",
      "    line = AMIGA->vhposr & 0xff00;",
      "    while (!(AMIGA->intreqr & bit))",
      "    {",
      "\tnow = AMIGA->vhposr & 0xff00;",
      "\tif (now != line)",
      "\t{",
      "\t    line = now;",
      "\t    if (++lines >= AUDIO_INITLINES)",
      "\t\tbreak;",
      "\t}",
      "\tif (++reads >= AUDIO_INITREADS)",
      "\t    break;",
      "    }",
      "    return (AMIGA->intreqr & bit) ? 0 : -1;",
      "}"]),
    ("audioinit: before the busy-wait", 2,
     "8a1a1fc91a0f5b373618756abead73d6839566ab3da2466439519c50c8ce101c",
     ["\tif (" + MARK + "(audio_channel[i].dmadone))",
      "\t{\t/* the word was never taken: no audio interrupt from Paula */",
      "\t    AMIGA->intreq = AINTCLR | audio_channel[i].dmadone;",
      "\t    audio_initerr = 1;",
      "\t    return;",
      "\t}"]),
    ("audioopen: the call of audioinit", 1,
     "2cc7e8dc09d7724d4fd696c822fd5fbed268e30840da7715b7efdb204621aba1",
     ["    if (audio_initerr)\t/* Paula is silent; nothing else is set up yet */",
      "    {",
      "\taudio_initerr = 0;",
      "\tsplx(s);",
      "\tkmem_free(acl, sizeof(*acl));",
      "\treturn EIO;",
      "    }"]),
]


def find(lines, n, digest, what):
    hits = [i for i in range(len(lines) - n + 1)
            if hashlib.sha256("".join(lines[i:i + n]).encode("latin-1")).hexdigest() == digest]
    if len(hits) != 1:
        sys.exit(f"[FAIL] audio.c: {what}: fingerprint found {len(hits)} times, expected 1 "
                 "(not the stock AMIX 2.1c audio.c?)")
    return hits[0]


path = sys.argv[1]
# split at newlines only: the stock file has form feeds between functions,
# which splitlines() would treat as line ends as well
lines = open(path, encoding="latin-1", newline="").read().split("\n")
lines = [l + "\n" for l in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
if any(MARK in l for l in lines):
    sys.exit("[FAIL] audio.c already carries the bounded audioinit() wait")

# locate everything first, then insert from the bottom up so positions hold
at = sorted(((find(lines, n, d, what) + n, new) for what, n, d, new in PLACES), reverse=True)
for pos, new in at:
    lines[pos:pos] = [l + "\n" for l in new]
open(path, "w", encoding="latin-1").write("".join(lines))
print("      [ok] audio.c: audioinit() waits a bounded time; a silent Paula fails the open (EIO)")
