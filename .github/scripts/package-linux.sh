#!/bin/sh
# Lay out a self-contained Linux build from a build tree on Debian or Ubuntu.
# Usage: package-linux.sh <build dir> <output dir>
# The output holds the launcher as blobby with its libraries in lib, and the game in app-<version>
# with its own lib and data, see doc/launcher.md. complete.txt in the version folder is written last.
# The programs and the libraries get an RPATH to their lib folder, so no LD_LIBRARY_PATH is needed.
# Libraries that every desktop has, or that must match the graphics driver and the C library of
# the system, are not bundled.
set -eu

build_dir=$1
out_dir=$2

for tool in patchelf dpkg-query ldd; do
	command -v "$tool" > /dev/null 2>&1 || { echo "$tool is missing" >&2; exit 1; }
done

source_dir=$(cd "$(dirname "$0")/../.." && pwd)
version=$(head -n 1 "$source_dir/version.txt")
echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "version.txt holds '$version'" >&2; exit 1; }
app_dir="$out_dir/app-$version"
[ -f "$build_dir/launcher/blobby-launcher" ] || { echo "the build has no launcher, configure it with BLOBBY_LAUNCHER=ON" >&2; exit 1; }

rm -rf "$out_dir"
mkdir -p "$app_dir/lib" "$app_dir/data" "$out_dir/lib"
cmake --install "$build_dir" --prefix "$out_dir/.install"
cp "$out_dir/.install/bin/blobby" "$out_dir/.install/bin/blobby-server" "$app_dir/"
cp "$out_dir/.install/share/blobby/"* "$app_dir/data/"
cp COPYING "$app_dir/"
cp "$build_dir/launcher/blobby-launcher" "$out_dir/blobby"
cp linux/register-join-links.sh linux/unregister-join-links.sh linux/blobby.png COPYING README.md "$out_dir/"
rm -rf "$out_dir/.install"

for file in blobby blobby-server data/rules.zip data/gfx.zip data/server.xml COPYING; do
	[ -f "$app_dir/$file" ] || { echo "the version folder is missing $file" >&2; exit 1; }
done
for file in blobby register-join-links.sh COPYING README.md; do
	[ -f "$out_dir/$file" ] || { echo "the package is missing $file" >&2; exit 1; }
done

# These come from the system and are never bundled. glibc, the GCC runtime, OpenGL, X11 and
# its extensions, xkbcommon, Wayland, ALSA, PulseAudio, PipeWire, D-Bus and systemd must match
# the machine, and an older copy next to a newer driver or sound server breaks it.
# The small libraries at the end are what those need, so the newer system copy must win.
system_libs='^(ld-linux.*|linux-vdso.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libGLESv2|libGLU|libdrm|libgbm|libX11|libX11-xcb|libX[a-z0-9]+|libxcb.*|libxkbcommon.*|libwayland-.*|libdecor-.*|libasound|libpulse.*|libpipewire-.*|libdbus-1|libsystemd|libudev|libz|libexpat|libffi|libgcrypt|libgpg-error|liblzma|liblz4|libzstd|libcap|libbsd|libmd)\.so'

# Copy the libraries that the programs in a folder need into its lib folder, give them all an
# RPATH to that folder and copy the copyright files of their packages into its licenses folder.
# Usage: bundle <folder> <program>...
bundle() {
	dir=$1
	shift
	# ldd resolves every library name to the file the loader would use.
	: > "$dir/.paths"
	: > "$dir/.programs"
	for exe in "$@"; do
		echo "$dir/$exe" >> "$dir/.programs"
		ldd "$dir/$exe" > "$dir/.ldd" || { echo "ldd failed for $exe" >&2; exit 1; }
		if grep -q 'not found' "$dir/.ldd"; then
			cat "$dir/.ldd" >&2
			echo "a library of $exe was not found" >&2
			exit 1
		fi
		sed -n 's/^[[:space:]]*\([^ ]*\) => \(\/[^ ]*\) .*$/\1 \2/p' "$dir/.ldd" >> "$dir/.paths"
	done

	# Start from the NEEDED entries of the programs and follow the NEEDED entries of each library
	# that is bundled. A library that only a system library needs is left to the system.
	: > "$dir/.todo"
	while read -r program; do
		patchelf --print-needed "$program" >> "$dir/.todo"
	done < "$dir/.programs"
	sort -u "$dir/.todo" -o "$dir/.todo"
	: > "$dir/.seen"
	: > "$dir/.pkgs"
	count=0
	while [ -s "$dir/.todo" ]; do
		name=$(head -n 1 "$dir/.todo")
		sed -i 1d "$dir/.todo"
		grep -qxF "$name" "$dir/.seen" && continue
		echo "$name" >> "$dir/.seen"
		if echo "$name" | grep -Eq "$system_libs"; then
			echo "from the system: $name"
			continue
		fi
		lib=$(awk -v n="$name" '$1 == n { print $2; exit }' "$dir/.paths")
		[ -n "$lib" ] || { echo "ldd did not resolve $name" >&2; exit 1; }
		cp -L "$lib" "$dir/lib/$name"
		chmod 644 "$dir/lib/$name"
		patchelf --set-rpath '$ORIGIN' "$dir/lib/$name"
		patchelf --print-needed "$dir/lib/$name" >> "$dir/.todo"
		count=$((count + 1))
		# Remember the package of the library for its copyright file.
		pkg=$(dpkg-query -S "$lib" 2>/dev/null | head -n 1 | cut -d: -f1) || true
		if [ -z "$pkg" ]; then
			# The path may go through the /lib symlink of a merged /usr.
			pkg=$(dpkg-query -S "$(readlink -f "$lib")" 2>/dev/null | head -n 1 | cut -d: -f1) || true
		fi
		[ -n "$pkg" ] || { echo "no package owns $lib" >&2; exit 1; }
		echo "$pkg" >> "$dir/.pkgs"
	done
	if [ "$count" -eq 0 ]; then
		echo "no libraries were bundled into $dir" >&2
		exit 1
	fi
	echo "bundled libraries in $dir:"
	ls -1 "$dir/lib"
	while read -r program; do
		patchelf --set-rpath '$ORIGIN/lib' "$program"
	done < "$dir/.programs"

	sort -u "$dir/.pkgs" | while read -r pkg; do
		mkdir -p "$dir/licenses/$pkg"
		cp "/usr/share/doc/$pkg/copyright" "$dir/licenses/$pkg/copyright" || { echo "$pkg has no copyright file" >&2; exit 1; }
		echo "license of $pkg"
	done

	# Check that the programs now find every library in the package or on the system.
	while read -r program; do
		if ldd "$program" | grep 'not found'; then
			echo "$program misses a library after packaging" >&2
			exit 1
		fi
	done < "$dir/.programs"
	rm -f "$dir/.ldd" "$dir/.paths" "$dir/.programs" "$dir/.todo" "$dir/.seen" "$dir/.pkgs"
}

bundle "$app_dir" blobby blobby-server
bundle "$out_dir" blobby
# Monocypher is built into the launcher.
mkdir -p "$out_dir/licenses/monocypher"
cp deps/monocypher/LICENCE.md "$out_dir/licenses/monocypher/"

# The launcher starts a version folder only once this file is there, so it comes last.
echo "The launcher starts this version only when this file exists." > "$app_dir/complete.txt"
