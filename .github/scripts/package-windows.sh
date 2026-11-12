#!/bin/sh
# Lay out a self-contained Windows build from an MSYS2 UCRT64 build tree.
# Usage: package-windows.sh <build dir> <output dir>
# It runs in the MSYS2 shell, because it asks pacman which package owns each DLL.
# The output holds the launcher as blobby.exe with its DLLs, and the game in app-<version>,
# see doc/launcher.md. complete.txt in the version folder is written last.
set -eu

build_dir=$1
out_dir=$2

command -v pacman > /dev/null 2>&1 || { echo "pacman is missing, run this in the MSYS2 shell" >&2; exit 1; }

source_dir=$(cd "$(dirname "$0")/../.." && pwd)
version=$(head -n 1 "$source_dir/version.txt" | tr -d '\r')
echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "version.txt holds '$version'" >&2; exit 1; }
app_dir="$out_dir/app-$version"

rm -rf "$out_dir"
cmake --install "$build_dir" --prefix "$app_dir"
[ -f "$build_dir/launcher/blobby-launcher.exe" ] || { echo "the build has no launcher, configure it with BLOBBY_LAUNCHER=ON" >&2; exit 1; }

# The files that players open stay next to the launcher.
for file in register-join-links.cmd unregister-join-links.cmd README.md tournament-server.ps1; do
	mv "$app_dir/$file" "$out_dir/"
done
cp "$app_dir/COPYING" "$out_dir/"
cp "$build_dir/launcher/blobby-launcher.exe" "$out_dir/blobby.exe"

for file in blobby.exe blobby-server.exe data/rules.zip data/gfx.zip COPYING; do
	[ -f "$app_dir/$file" ] || { echo "the version folder is missing $file" >&2; exit 1; }
done
for file in blobby.exe COPYING README.md tournament-server.ps1 register-join-links.cmd unregister-join-links.cmd; do
	[ -f "$out_dir/$file" ] || { echo "the package is missing $file" >&2; exit 1; }
done

# Copy every DLL that the programs in a folder need from the MSYS2 prefix into that folder,
# and the license texts of their packages into its licenses folder.
# Usage: bundle <folder> <program>...
bundle() {
	dir=$1
	shift
	count=0
	: > "$dir/.dlls"
	: > "$dir/.pkgs"
	# ldd lists the dependencies recursively and resolves them through PATH.
	for exe in "$@"; do
		ldd "$dir/$exe" > "$dir/.ldd" || { echo "ldd failed for $exe" >&2; exit 1; }
		sed -n 's/^.*=> \(.*\/ucrt64\/bin\/[^ ]*\.[dD][lL][lL]\) .*$/\1/p' "$dir/.ldd" >> "$dir/.dlls"
	done
	sort -u "$dir/.dlls" > "$dir/.dlls.sorted"
	while read -r dll; do
		echo "bundling $dll into $dir"
		cp "$dll" "$dir/"
		count=$((count + 1))
		# Remember the MSYS2 package of the DLL for its license.
		pacman -Qqo "$dll" >> "$dir/.pkgs" || { echo "no package owns $dll" >&2; exit 1; }
	done < "$dir/.dlls.sorted"
	if [ "$count" -eq 0 ]; then
		echo "no DLLs were bundled into $dir" >&2
		exit 1
	fi

	sort -u "$dir/.pkgs" > "$dir/.pkgs.sorted"
	while read -r pkg; do
		if ! pacman -Qlq "$pkg" | grep '/share/licenses/.*[^/]$' > "$dir/.licenses"; then
			# Some packages ship no license text. Write down the license and the project page instead.
			name=${pkg#mingw-w64-ucrt-x86_64-}
			mkdir -p "$dir/licenses/$name"
			pacman -Qi "$pkg" | grep -E '^(Name|Version|URL|Licenses) ' > "$dir/licenses/$name/LICENSE-INFO.txt"
			echo "$pkg has no license files in share/licenses, wrote LICENSE-INFO.txt" >&2
			continue
		fi
		while read -r file; do
			target="$dir/licenses/${file#*/share/licenses/}"
			mkdir -p "$(dirname "$target")"
			cp "$file" "$target"
		done < "$dir/.licenses"
		echo "license of $pkg"
	done < "$dir/.pkgs.sorted"
	rm -f "$dir/.ldd" "$dir/.dlls" "$dir/.dlls.sorted" "$dir/.pkgs" "$dir/.pkgs.sorted" "$dir/.licenses"
}

bundle "$app_dir" blobby.exe blobby-server.exe
bundle "$out_dir" blobby.exe
# Monocypher is built into the launcher.
mkdir -p "$out_dir/licenses/monocypher"
cp "$source_dir/deps/monocypher/LICENCE.md" "$out_dir/licenses/monocypher/"

# The launcher starts a version folder only once this file is there, so it comes last.
echo "The launcher starts this version only when this file exists." > "$app_dir/complete.txt"
