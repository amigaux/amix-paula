#!/bin/sh
# install.sh PORTDIR -- copy the /dev/paula files into a checkout of
# amix-040-060-port (https://github.com/asokero/amix-040-060-port).
# Refuses to overwrite a file that exists with different contents; -f does.
#
# Copyright (c) 2026 isoriano1968; changes (c) 2026 Jussi Alanärä.
# MIT licence, see LICENSE.
set -e
force=0
[ "$1" = -f ] && { force=1; shift; }
PORT=$1
[ -n "$PORT" ] && [ -f "$PORT/relink-040.sh" ] && [ -f "$PORT/tools/config-load.sh" ] || {
	echo "usage: $0 [-f] /path/to/amix-040-060-port"; exit 1; }
PORT=$(cd "$PORT" && pwd)		# absolute: the copies run from port/
SRC=$(cd "$(dirname "$0")/port" && pwd)
# one path per line, names with blanks included
list() { (cd "$SRC" && find . -type f | sed 's|^\./||' | LC_ALL=C sort); }
if [ $force = 0 ]; then
	list | while IFS= read -r f; do
		if [ -f "$PORT/$f" ] && ! cmp -s "$SRC/$f" "$PORT/$f"; then
			echo "exists and differs: $PORT/$f (-f to overwrite)"; exit 1
		fi
	done || exit 1
fi
list | while IFS= read -r f; do
	mkdir -p "$PORT/$(dirname "$f")"
	cp "$SRC/$f" "$PORT/$f"
	echo "  $f"
done || exit 1
echo "installed into $PORT"
