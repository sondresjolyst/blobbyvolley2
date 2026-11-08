#!/bin/sh
# Lay out a self-contained Linux build from a build tree on Debian or Ubuntu.
# Usage: package-linux.sh <build dir> <output dir>
# The programs and the libraries get an RPATH to the lib folder, so no launcher or
# LD_LIBRARY_PATH is needed. Libraries that every desktop has, or that must match the
# graphics driver and the C library of the system, are not bundled.
set -eu

build_dir=$1
out_dir=$2

for tool in patchelf dpkg-query ldd; do
	command -v "$tool" > /dev/null 2>&1 || { echo "$tool is missing" >&2; exit 1; }
done

rm -rf "$out_dir"
mkdir -p "$out_dir/lib" "$out_dir/data"
cmake --install "$build_dir" --prefix "$out_dir/.install"
cp "$out_dir/.install/bin/blobby" "$out_dir/.install/bin/blobby-server" "$out_dir/"
cp "$out_dir/.install/share/blobby/"* "$out_dir/data/"
cp linux/register-join-links.sh linux/unregister-join-links.sh linux/blobby.png COPYING README.md "$out_dir/"
rm -rf "$out_dir/.install"

for file in blobby blobby-server data/rules.zip data/gfx.zip data/server.xml COPYING; do
	[ -f "$out_dir/$file" ] || { echo "the package is missing $file" >&2; exit 1; }
done

# These come from the system. glibc, the GCC runtime, the graphics stack, X11, Wayland, ALSA,
# D-Bus and systemd must match the machine, and an older copy next to a newer driver breaks it.
# The small libraries at the end are what those need, so the newer system copy must win.
system_libs='^(ld-linux.*|linux-vdso.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libGLESv2|libdrm|libgbm|libX11|libX11-xcb|libxcb.*|libXau|libXdmcp|libasound|libdbus-1|libsystemd|libudev|libwayland-.*|libz|libexpat|libffi|libgcrypt|libgpg-error|liblzma|liblz4|libzstd|libcap|libbsd|libmd)\.so'

# Copy every other library that the programs need. ldd lists them recursively.
: > "$out_dir/.libs"
for exe in blobby blobby-server; do
	ldd "$out_dir/$exe" > "$out_dir/.ldd" || { echo "ldd failed for $exe" >&2; exit 1; }
	if grep -q 'not found' "$out_dir/.ldd"; then
		cat "$out_dir/.ldd" >&2
		echo "a library of $exe was not found" >&2
		exit 1
	fi
	sed -n 's/^.*=> \(\/[^ ]*\) .*$/\1/p' "$out_dir/.ldd" >> "$out_dir/.libs"
done
sort -u "$out_dir/.libs" > "$out_dir/.libs.sorted"
count=0
: > "$out_dir/.pkgs"
while read -r lib; do
	name=$(basename "$lib")
	if echo "$name" | grep -Eq "$system_libs"; then
		echo "from the system: $name"
		continue
	fi
	echo "bundling $lib"
	cp -L "$lib" "$out_dir/lib/$name"
	chmod 644 "$out_dir/lib/$name"
	patchelf --set-rpath '$ORIGIN' "$out_dir/lib/$name"
	count=$((count + 1))
	# Remember the package of the library for its copyright file.
	pkg=$(dpkg-query -S "$lib" 2>/dev/null | head -n 1 | cut -d: -f1) || true
	if [ -z "$pkg" ]; then
		# The path may go through the /lib symlink of a merged /usr.
		pkg=$(dpkg-query -S "$(readlink -f "$lib")" 2>/dev/null | head -n 1 | cut -d: -f1) || true
	fi
	[ -n "$pkg" ] || { echo "no package owns $lib" >&2; exit 1; }
	echo "$pkg" >> "$out_dir/.pkgs"
done < "$out_dir/.libs.sorted"
if [ "$count" -eq 0 ]; then
	echo "no libraries were bundled" >&2
	exit 1
fi
patchelf --set-rpath '$ORIGIN/lib' "$out_dir/blobby"
patchelf --set-rpath '$ORIGIN/lib' "$out_dir/blobby-server"

# Copy the copyright files of those packages into licenses.
sort -u "$out_dir/.pkgs" | while read -r pkg; do
	mkdir -p "$out_dir/licenses/$pkg"
	cp "/usr/share/doc/$pkg/copyright" "$out_dir/licenses/$pkg/copyright" || { echo "$pkg has no copyright file" >&2; exit 1; }
	echo "license of $pkg"
done
rm -f "$out_dir/.ldd" "$out_dir/.libs" "$out_dir/.libs.sorted" "$out_dir/.pkgs"

# Check that the programs now find every library in the package or on the system.
for exe in blobby blobby-server; do
	if ldd "$out_dir/$exe" | grep 'not found'; then
		echo "$exe misses a library after packaging" >&2
		exit 1
	fi
done
