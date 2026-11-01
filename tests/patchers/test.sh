#!/bin/sh
# tests/patchers/test.sh -- the three audio.c patchers on the stock
# audio.c: each of the six orders applies, a second run of any patcher is
# refused and leaves the file as it was, and the kernel cuts' order
# (safeclose, initwait, paula) gives the audio.c they compile.
# AUDIO_C names the stock file (default: amigaux/amix-system cloned beside
# this repo, usr/sys/amiga/driver/audio.c); without it the test is skipped.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
TOP=$(cd "$(dirname "$0")/../.." && pwd)
AUDIO_C=${AUDIO_C:-$TOP/../amix-system/usr/sys/amiga/driver/audio.c}
CUT=1f43c5ff6558bf6583bcc3525b456bf3560e2bcf6100f5c736b0aa0221b260cc
W="$TOP/build/tests/patchers"
[ -f "$AUDIO_C" ] || { echo "patchers: skipped (no stock audio.c; set AUDIO_C)"; exit 0; }
rm -rf "${W:?}"; mkdir -p "$W"
fail=0
bad() { echo "FAIL patchers: $*"; fail=1; }
for o in "safeclose initwait paula" "safeclose paula initwait" "initwait safeclose paula" \
	 "initwait paula safeclose" "paula safeclose initwait" "paula initwait safeclose"; do
	cp "$AUDIO_C" "$W/audio.c"
	for p in $o; do
		python3 "$TOP/port/src/patch_audio_$p.py" "$W/audio.c" > "$W/log" 2>&1 ||
			bad "$p refused in the order $o: $(tail -1 "$W/log")"
	done
	for p in safeclose initwait paula; do
		cp "$W/audio.c" "$W/again.c"
		python3 "$TOP/port/src/patch_audio_$p.py" "$W/again.c" > "$W/log" 2>&1 &&
			bad "$p applied twice ($o)"
		cmp -s "$W/audio.c" "$W/again.c" || bad "$p's refusal changed the file ($o)"
	done
	h=$(sha256sum < "$W/audio.c" | cut -c1-64)
	echo "  ok: $o -> ${h%${h#????????}}"
	[ "$o" = "safeclose initwait paula" ] && [ "$h" != "$CUT" ] && bad "the cuts' order gives $h, not $CUT"
done
[ $fail = 0 ] && echo "patchers: all passed"
exit $fail
