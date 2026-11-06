#!/bin/sh
# Lay out a self-contained Windows build from an MSYS2 UCRT64 build tree.
# Usage: package-windows.sh <build dir> <output dir>
set -eu

build_dir=$1
out_dir=$2

rm -rf "$out_dir"
cmake --install "$build_dir" --prefix "$out_dir"

# Copy every DLL that the programs need from the MSYS2 prefix.
# ldd lists the dependencies recursively and resolves them through PATH.
for exe in blobby.exe blobby-server.exe; do
	ldd "$out_dir/$exe" | sed -n 's/^.*=> \(.*\/ucrt64\/bin\/[^ ]*\.[dD][lL][lL]\) .*$/\1/p'
done | sort -u | while read -r dll; do
	echo "bundling $dll"
	cp "$dll" "$out_dir/"
done
