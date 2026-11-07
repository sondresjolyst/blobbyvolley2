#!/bin/sh
# Stops blobby:// join links from opening Blobby Volley 2 for the current user.
# It removes the desktop file that register-join-links.sh wrote and its entry in mimeapps.list.
set -eu

apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
rm -f "$apps/blobby-join.desktop"

list=${XDG_CONFIG_HOME:-$HOME/.config}/mimeapps.list
if [ -f "$list" ]; then
	sed -i '/^x-scheme-handler\/blobby=blobby-join\.desktop;\{0,1\}$/d' "$list"
fi

if command -v update-desktop-database > /dev/null 2>&1; then
	update-desktop-database "$apps" || true
fi
echo "Join links no longer open Blobby Volley 2"
