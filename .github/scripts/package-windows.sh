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
count=0
for exe in blobby.exe blobby-server.exe; do
	ldd "$out_dir/$exe" > "$out_dir/.ldd" || { echo "ldd failed for $exe" >&2; exit 1; }
	sed -n 's/^.*=> \(.*\/ucrt64\/bin\/[^ ]*\.[dD][lL][lL]\) .*$/\1/p' "$out_dir/.ldd" >> "$out_dir/.dlls"
done
sort -u "$out_dir/.dlls" > "$out_dir/.dlls.sorted"
while read -r dll; do
	echo "bundling $dll"
	cp "$dll" "$out_dir/"
	count=$((count + 1))
done < "$out_dir/.dlls.sorted"
rm -f "$out_dir/.ldd" "$out_dir/.dlls" "$out_dir/.dlls.sorted"
if [ "$count" -eq 0 ]; then
	echo "no DLLs were bundled" >&2
	exit 1
fi
