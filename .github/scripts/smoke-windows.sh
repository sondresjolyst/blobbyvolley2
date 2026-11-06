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
winpid=$(cat "/proc/$pid/winpid" 2>/dev/null || echo "$pid")
# Never leave the program running, even if this script exits early.
trap 'kill -9 "$pid" 2>/dev/null || taskkill //F //PID "$winpid" >/dev/null 2>&1 || true' EXIT

sleep "$seconds"
if kill -0 "$pid" 2>/dev/null; then
	echo "$exe stayed up for $seconds seconds"
	kill "$pid" 2>/dev/null || taskkill //F //PID "$winpid" >/dev/null 2>&1 || true
	# Wait for it to stop, but only for a bounded time.
	i=0
	while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 10 ]; do
		sleep 1
		i=$((i + 1))
	done
	rm -f "smoke-$exe.log"
else
	echo "$exe exited early"
	cat "smoke-$exe.log"
	exit 1
fi
