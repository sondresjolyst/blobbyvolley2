#!/bin/sh
# Start a packaged program with a PATH that has no MSYS2 prefix and check that it stays up.
# Usage: smoke-windows.sh <package dir> <exe name> [seconds]
set -eu

pkg_dir=$1
exe=$2
seconds=${3:-5}

cd "$pkg_dir"
env PATH="/c/Windows/System32:/c/Windows" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "./$exe" > "smoke-$exe.log" 2>&1 &
pid=$!
sleep "$seconds"
if kill -0 "$pid" 2>/dev/null; then
	echo "$exe stayed up for $seconds seconds"
	kill "$pid" || true
	wait "$pid" 2>/dev/null || true
	rm -f "smoke-$exe.log"
else
	echo "$exe exited early"
	cat "smoke-$exe.log"
	exit 1
fi
