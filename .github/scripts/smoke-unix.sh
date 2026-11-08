#!/bin/sh
# Start a packaged program without a window or sound and check that it stays up and stops when asked.
# Usage: smoke-unix.sh <working dir> <seconds> <program> [arguments...]
# The program path is relative to the working dir. It runs on Linux and macOS.
set -eu

work_dir=$1
seconds=$2
shift 2
exe=$1
shift

cd "$work_dir"
log=$(mktemp)
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "./$exe" "$@" > "$log" 2>&1 &
pid=$!
# Never leave the program running, even if this script exits early.
trap 'kill -9 "$pid" 2>/dev/null || true' EXIT

sleep "$seconds"
if ! kill -0 "$pid" 2>/dev/null; then
	echo "$exe exited early"
	cat "$log"
	exit 1
fi
echo "$exe stayed up for $seconds seconds"
kill "$pid"
# Wait for it to stop, but only for a bounded time.
i=0
while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 10 ]; do
	sleep 1
	i=$((i + 1))
done
if kill -0 "$pid" 2>/dev/null; then
	echo "$exe did not stop within 10 seconds of SIGTERM"
	cat "$log"
	exit 1
fi
status=0
wait "$pid" || status=$?
if [ "$status" -ne 0 ]; then
	echo "$exe stopped with status $status"
	cat "$log"
	exit 1
fi
echo "$exe stopped cleanly"
rm -f "$log"
