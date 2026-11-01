#!/bin/sh
# tests/cdevsw/test.sh -- port/src/patch_paula_cdevsw.py against a
# synthetic kernel: a .data cdevsw of 70 52-byte rows whose function
# fields name nodev (and the flag field nullflag), as the port's kernels
# have it, ld -r'd with paulaopen/paulaclose/paulaioctl.  Checks that
# slot 54 is retargeted and nothing else, that a second run changes
# nothing, and that a used slot, a wrong entry size and a table too short
# are refused.  KERNEL=<a relocatable static k46 kernel> also runs it on
# that kernel.  Needs m68k-cbm-sysv4-as/ld and m68k-linux-gnu-readelf.
#
# Copyright (c) 2026 Jussi Alanärä.  MIT licence, see LICENSE.
set -e
TOP=$(cd "$(dirname "$0")/../.." && pwd)
W="$TOP/build/tests/cdevsw"
P="$TOP/port/src/patch_paula_cdevsw.py"
RE=m68k-linux-gnu-readelf
rm -rf "${W:?}"; mkdir -p "$W"
fail=0
ok() { echo "  ok: $*"; }
bad() { echo "FAIL cdevsw: $*"; fail=1; }

# kern <name> <rows> <entsize words> [<row> <field> <symbol>]
kern() {
	n=$1 rows=$2 words=$3
	{
		printf '\t.text\n\t.globl nodev\nnodev:\trts\n\t.globl nullflag\nnullflag:\trts\n'
		printf '\t.globl other\nother:\trts\n'
		printf '\t.data\n\t.globl cdevcnt\n\t.type cdevcnt,@object\n\t.size cdevcnt,4\n'
		printf 'cdevcnt:\t.long %d\n' "$rows"
		printf '\t.globl cdevsw\n\t.type cdevsw,@object\n\t.size cdevsw,%d\ncdevsw:\n' $((rows * words * 4))
		r=0
		while [ $r -lt $rows ]; do
			f=0
			while [ $f -lt $words ]; do
				s=nodev
				[ $f -ge 10 ] && s=0
				[ $f -eq $((words - 1)) ] && s=nullflag
				[ "$r" = "${4:-x}" ] && [ "$f" = "${5:-x}" ] && s=$6
				printf '\t.long %s\n' "$s"
				f=$((f + 1))
			done
			r=$((r + 1))
		done
	} > "$W/$n.s"
	printf '\t.text\n\t.globl paulaopen\npaulaopen:\trts\n\t.globl paulaclose\npaulaclose:\trts\n\t.globl paulaioctl\npaulaioctl:\trts\n' > "$W/paula.s"
	m68k-cbm-sysv4-as -o "$W/$n.o" "$W/$n.s"
	m68k-cbm-sysv4-as -o "$W/paula.o" "$W/paula.s"
	m68k-cbm-sysv4-ld -r -o "$W/$n" "$W/$n.o" "$W/paula.o"
}

# the relocations of .rela.data as "<row> <field> <symbol>", cdevsw at $2
rows() {
	$RE -rW "$1" | awk -v base="$2" '/Relocation section .\.rela\.data/ { p = 1; next }
		/Relocation section/ { p = 0 } p && $1 ~ /^[0-9a-f]+$/ {
		o = strtonum("0x" $1) - base; if (o >= 0) print int(o / 52), (o % 52) / 4, $5 }'
}

cdevsw_at() { $RE -sW "$1" | awk '$8 == "cdevsw" { print strtonum("0x" $2) }'; }

kern good 70 13
cp "$W/good" "$W/good.orig"
python3 "$P" "$W/good" > "$W/good.log" || bad "patcher refused the synthetic kernel"
B=$(cdevsw_at "$W/good")
rows "$W/good.orig" "$B" > "$W/before"
rows "$W/good" "$B" > "$W/after"
diff "$W/before" "$W/after" > "$W/diff" || true
if [ "$(grep -c '^>' "$W/diff")" -eq 3 ] &&
   grep -q '^> 54 0 paulaopen$' "$W/diff" && grep -q '^> 54 1 paulaclose$' "$W/diff" &&
   grep -q '^> 54 4 paulaioctl$' "$W/diff"; then
	ok "slot 54: d_open, d_close, d_ioctl retargeted, nothing else"
else
	bad "unexpected retarget:"; cat "$W/diff"
fi
cp "$W/good" "$W/good.1"
python3 "$P" "$W/good" > "$W/good2.log" || bad "second run refused"
cmp -s "$W/good" "$W/good.1" && grep -q skip "$W/good2.log" && ok "second run: no change" ||
	bad "second run changed the kernel"

refuse() {	# refuse <what> <kernel>
	cp "$W/$2" "$W/$2.orig"
	if python3 "$P" "$W/$2" > "$W/$2.log" 2>&1; then bad "$1: patched"
	elif ! cmp -s "$W/$2" "$W/$2.orig"; then bad "$1: refused but changed the file"
	else ok "$1: refused ($(tail -1 "$W/$2.log" | cut -c1-60))"; fi
}
kern used 70 13 54 2 other;	refuse "slot 54 in use (d_read)" used
kern size 70 14;		refuse "56-byte entries" size
kern short 54 13;		refuse "table of 54 rows" short

if [ -n "$KERNEL" ]; then
	cp "$KERNEL" "$W/real.o0"
	m68k-cbm-sysv4-ld -r -o "$W/real" "$W/real.o0" "$W/paula.o"
	cp "$W/real" "$W/real.orig"
	python3 "$P" "$W/real" > "$W/real.log" || bad "patcher refused $KERNEL"
	B=$(cdevsw_at "$W/real")
	rows "$W/real.orig" "$B" > "$W/rbefore"
	rows "$W/real" "$B" > "$W/rafter"
	diff "$W/rbefore" "$W/rafter" > "$W/rdiff" || true
	[ "$(grep -c '^>' "$W/rdiff")" -eq 3 ] && grep -q '^> 54 0 paulaopen$' "$W/rdiff" &&
		ok "$(basename "$KERNEL"): slot 54 retargeted, nothing else" || { bad "real kernel:"; cat "$W/rdiff"; }
fi
[ $fail = 0 ] && echo "cdevsw: all passed"
exit $fail
