#!/bin/sh
# Lay out a self-contained Windows build from an MSYS2 UCRT64 build tree.
# Usage: package-windows.sh <build dir> <output dir>
# It runs in the MSYS2 shell, because it asks pacman which package owns each DLL.
set -eu

build_dir=$1
out_dir=$2

command -v pacman > /dev/null 2>&1 || { echo "pacman is missing, run this in the MSYS2 shell" >&2; exit 1; }

rm -rf "$out_dir"
cmake --install "$build_dir" --prefix "$out_dir"

for file in blobby.exe blobby-server.exe COPYING README.md tournament-server.ps1 register-join-links.cmd unregister-join-links.cmd; do
	[ -f "$out_dir/$file" ] || { echo "the package is missing $file" >&2; exit 1; }
done

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
	# Remember the MSYS2 package of the DLL for its license.
	pacman -Qqo "$dll" >> "$out_dir/.pkgs" || { echo "no package owns $dll" >&2; exit 1; }
done < "$out_dir/.dlls.sorted"
if [ "$count" -eq 0 ]; then
	echo "no DLLs were bundled" >&2
	exit 1
fi

# Copy the license texts of those packages from share/licenses into licenses.
sort -u "$out_dir/.pkgs" > "$out_dir/.pkgs.sorted"
while read -r pkg; do
	if ! pacman -Qlq "$pkg" | grep '/share/licenses/.*[^/]$' > "$out_dir/.licenses"; then
		# Some packages ship no license text. Write down the license and the project page instead.
		name=${pkg#mingw-w64-ucrt-x86_64-}
		mkdir -p "$out_dir/licenses/$name"
		pacman -Qi "$pkg" | grep -E '^(Name|Version|URL|Licenses) ' > "$out_dir/licenses/$name/LICENSE-INFO.txt"
		echo "$pkg has no license files in share/licenses, wrote LICENSE-INFO.txt" >&2
		continue
	fi
	while read -r file; do
		target="$out_dir/licenses/${file#*/share/licenses/}"
		mkdir -p "$(dirname "$target")"
		cp "$file" "$target"
	done < "$out_dir/.licenses"
	echo "license of $pkg"
done < "$out_dir/.pkgs.sorted"
rm -f "$out_dir/.ldd" "$out_dir/.dlls" "$out_dir/.dlls.sorted" "$out_dir/.pkgs" "$out_dir/.pkgs.sorted" "$out_dir/.licenses"
