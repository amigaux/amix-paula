#!/usr/bin/env python3
"""patch_audio_safeclose.py -- /dev/audio lets go of its channels on close.
Applied to a build-time copy of the stock AMIX 2.1c audio driver
(usr/sys/amiga/driver/audio.c), before or after patch_audio_paula.py.
Usage: patch_audio_safeclose.py audio.c

The stock audioclose() takes the client off the client list and frees it,
but leaves it in audio_channel[].client of the channel it was playing on.
If the close comes mid-stream (the program killed), the next audiointr()
reads the freed client (acl->repeat, acl->q, acl->mp), and the channel can
stay claimed for good: /dev/audio runs out of channels, and /dev/paula,
which takes all four, finds them still interrupting.

The fix stops each channel the client holds before it is freed, at the
audio interrupt's level (audioclose() already holds splaudio there): its
interrupt and DMA off, its request cleared, the channel free and empty,
and a message the client still held freed.

Commodore's source is not in this repository, and neither is any of its
text.  The place is found as patch_audio_paula.py finds its places: the
SHA-256 of the line exactly as the stock file has it, which must match
exactly once; our lines go in after it.

  audioclose(): the client leaves the list  -> the channels it holds stop

Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
"""
import hashlib
import sys

ANCHOR = ("audioclose: client leaves the list", 1,
          "ad5e45679fcff3091ef7f584a750ca0f81578b1606320bbcf9f6dc868a18bba1")
MARK = "safe close: the channels this client holds stop"
OURS = [
    "    {\t/* " + MARK + " (amix-paula) */",
    "\tint i;",
    "",
    "\tfor (i = 0; i < MAXCHANNELS; ++i)",
    "\t    if (audio_channel[i].client == acl)",
    "\t    {",
    "\t\tAMIGA->intena = AINTCLR | audio_channel[i].dmadone;",
    "\t\tAMIGA->dmacon = DMACLR | audio_channel[i].enable;",
    "\t\tAMIGA->intreq = AINTCLR | audio_channel[i].dmadone;",
    "\t\taudio_channel[i].audio->loud = 0;",
    "\t\taudio_channel[i].client = NULL;",
    "\t\taudio_channel[i].pending = 0;",
    "\t\taudio_channel[i].used = 0;",
    "\t\taudio_channel[i].nbytes[0] = audio_channel[i].nbytes[1] =",
    "\t\t    audio_channel[i].nbytes[2] = 0;",
    "\t    }",
    "\tif (acl->mp != NULL)",
    "\t{",
    "\t    freemsg(acl->mp);",
    "\t    acl->mp = NULL;",
    "\t}",
    "    }",
]

path = sys.argv[1]
# split at newlines only: the stock file has form feeds between functions
lines = open(path, encoding="latin-1", newline="").read().split("\n")
lines = [l + "\n" for l in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
if any(MARK in l for l in lines):
    sys.exit("[FAIL] audio.c already carries the safe close")
what, n, digest = ANCHOR
hits = [i for i in range(len(lines) - n + 1)
        if hashlib.sha256("".join(lines[i:i + n]).encode("latin-1")).hexdigest() == digest]
if len(hits) != 1:
    sys.exit(f"[FAIL] audio.c: {what}: fingerprint found {len(hits)} times, expected 1 "
             "(not the stock AMIX 2.1c audio.c?)")
pos = hits[0] + n
lines[pos:pos] = [l + "\n" for l in OURS]
open(path, "w", encoding="latin-1").write("".join(lines))
print("      [ok] audio.c: safe close (the client's channels stop before it is freed)")
