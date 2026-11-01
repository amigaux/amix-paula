#!/bin/sh
# tests/install/test.sh -- install.sh: a relative PORTDIR given from
# another directory, a name with a blank, the refusal to overwrite a
# different file, and -f.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
TOP=$(cd "$(dirname "$0")/../.." && pwd)
W="$TOP/build/tests/install"
rm -rf "${W:?}"; mkdir -p "$W/repo/port/src" "$W/run" "$W/port/tools"
fail=0
bad() { echo "FAIL install: $*"; fail=1; }
ok() { echo "  ok: $*"; }
cp "$TOP/install.sh" "$W/repo/"
echo one > "$W/repo/port/a b.txt"
echo two > "$W/repo/port/src/c.py"
touch "$W/port/relink-040.sh" "$W/port/tools/config-load.sh"
cd "$W/run" || exit 1
sh ../repo/install.sh ../port > "$W/log1" 2>&1 || bad "relative PORTDIR refused: $(cat "$W/log1")"
cmp -s "$W/repo/port/a b.txt" "$W/port/a b.txt" && cmp -s "$W/repo/port/src/c.py" "$W/port/src/c.py" &&
	ok "relative PORTDIR from another directory, a name with a blank" || bad "files not copied"
echo changed > "$W/port/src/c.py"
if sh ../repo/install.sh ../port > "$W/log2" 2>&1; then bad "overwrote a different file"
else grep -q "exists and differs" "$W/log2" && [ "$(cat "$W/port/src/c.py")" = changed ] &&
	ok "a different file: refused, untouched" || bad "refusal: $(cat "$W/log2")"; fi
sh ../repo/install.sh -f ../port > "$W/log3" 2>&1 && cmp -s "$W/repo/port/src/c.py" "$W/port/src/c.py" &&
	ok "-f overwrites" || bad "-f: $(cat "$W/log3")"
sh ../repo/install.sh ../nowhere > "$W/log4" 2>&1 && bad "accepted a missing PORTDIR" ||
	ok "a missing PORTDIR: usage"
[ $fail = 0 ] && echo "install: all passed"
exit $fail
