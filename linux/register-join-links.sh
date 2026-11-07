#!/bin/sh
# Lets blobby:// join links from the tournament site open Blobby Volley 2 for the current user.
# Usage: register-join-links.sh [path to blobby] [--print]
# Without a path it uses the blobby found on PATH. With --print it only shows the desktop file.
# It writes ~/.local/share/applications/blobby-join.desktop and makes it the handler of
# x-scheme-handler/blobby with xdg-mime. It needs no root rights.
set -eu

print_only=false
game=""
for arg in "$@"; do
	case "$arg" in
		--print) print_only=true ;;
		*) game=$arg ;;
	esac
done

if [ -z "$game" ]; then
	game=$(command -v blobby || true)
fi
if [ -z "$game" ] || [ ! -f "$game" ]; then
	echo "Could not find the blobby program. Give its path as the first argument." >&2
	exit 1
fi
game=$(cd "$(dirname "$game")" && pwd)/$(basename "$game")

# A line break or another control character cannot be written into a desktop file safely.
case "$game" in
	*[[:cntrl:]]*)
		echo "The path of the game holds a control character: $game" >&2
		exit 1
		;;
esac

# Quote the path for the Exec key. Inside the quotes \ " ` and $ get a backslash, and the
# desktop file format then doubles every backslash. A % is written as %%.
quoted=$(printf '%s' "$game" | sed -e 's/\\/\\\\\\\\/g' -e 's/"/\\\\"/g' -e 's/`/\\\\`/g' -e 's/\$/\\\\$/g' -e 's/%/%%/g')

desktop="[Desktop Entry]
Type=Application
Name=Blobby Volley 2 join link
Comment=Opens tournament join links in Blobby Volley 2
Icon=blobby
Exec=\"$quoted\" %u
MimeType=x-scheme-handler/blobby;
NoDisplay=true
Terminal=false"

if [ "$print_only" = true ]; then
	printf '%s\n' "$desktop"
	exit 0
fi

apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$apps"
printf '%s\n' "$desktop" > "$apps/blobby-join.desktop.tmp"
mv "$apps/blobby-join.desktop.tmp" "$apps/blobby-join.desktop"

if command -v update-desktop-database > /dev/null 2>&1; then
	update-desktop-database "$apps" || true
fi
xdg-mime default blobby-join.desktop x-scheme-handler/blobby
echo "Join links now open $game"
