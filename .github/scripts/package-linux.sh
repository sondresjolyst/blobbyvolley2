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

# These come from the system and are never bundled. glibc, the GCC runtime, OpenGL, X11 and
# its extensions, xkbcommon, Wayland, ALSA, PulseAudio, PipeWire, D-Bus and systemd must match
# the machine, and an older copy next to a newer driver or sound server breaks it.
# The small libraries at the end are what those need, so the newer system copy must win.
system_libs='^(ld-linux.*|linux-vdso.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libGLESv2|libGLU|libdrm|libgbm|libX11|libX11-xcb|libX[a-z0-9]+|libxcb.*|libxkbcommon.*|libwayland-.*|libdecor-.*|libasound|libpulse.*|libpipewire-.*|libdbus-1|libsystemd|libudev|libz|libexpat|libffi|libgcrypt|libgpg-error|liblzma|liblz4|libzstd|libcap|libbsd|libmd)\.so'

# ldd resolves every library name to the file the loader would use.
: > "$out_dir/.paths"
for exe in blobby blobby-server; do
	ldd "$out_dir/$exe" > "$out_dir/.ldd" || { echo "ldd failed for $exe" >&2; exit 1; }
	if grep -q 'not found' "$out_dir/.ldd"; then
		cat "$out_dir/.ldd" >&2
		echo "a library of $exe was not found" >&2
		exit 1
	fi
	sed -n 's/^[[:space:]]*\([^ ]*\) => \(\/[^ ]*\) .*$/\1 \2/p' "$out_dir/.ldd" >> "$out_dir/.paths"
done

# Start from the NEEDED entries of the programs and follow the NEEDED entries of each library
# that is bundled. A library that only a system library needs is left to the system.
patchelf --print-needed "$out_dir/blobby" "$out_dir/blobby-server" | sort -u > "$out_dir/.todo"
: > "$out_dir/.seen"
: > "$out_dir/.pkgs"
count=0
while [ -s "$out_dir/.todo" ]; do
	name=$(head -n 1 "$out_dir/.todo")
	sed -i 1d "$out_dir/.todo"
	grep -qxF "$name" "$out_dir/.seen" && continue
	echo "$name" >> "$out_dir/.seen"
	if echo "$name" | grep -Eq "$system_libs"; then
		echo "from the system: $name"
		continue
	fi
	lib=$(awk -v n="$name" '$1 == n { print $2; exit }' "$out_dir/.paths")
	[ -n "$lib" ] || { echo "ldd did not resolve $name" >&2; exit 1; }
	cp -L "$lib" "$out_dir/lib/$name"
	chmod 644 "$out_dir/lib/$name"
	patchelf --set-rpath '$ORIGIN' "$out_dir/lib/$name"
	patchelf --print-needed "$out_dir/lib/$name" >> "$out_dir/.todo"
	count=$((count + 1))
	# Remember the package of the library for its copyright file.
	pkg=$(dpkg-query -S "$lib" 2>/dev/null | head -n 1 | cut -d: -f1) || true
	if [ -z "$pkg" ]; then
		# The path may go through the /lib symlink of a merged /usr.
		pkg=$(dpkg-query -S "$(readlink -f "$lib")" 2>/dev/null | head -n 1 | cut -d: -f1) || true
	fi
	[ -n "$pkg" ] || { echo "no package owns $lib" >&2; exit 1; }
	echo "$pkg" >> "$out_dir/.pkgs"
done
if [ "$count" -eq 0 ]; then
	echo "no libraries were bundled" >&2
	exit 1
fi
echo "bundled libraries:"
ls -1 "$out_dir/lib"
patchelf --set-rpath '$ORIGIN/lib' "$out_dir/blobby"
patchelf --set-rpath '$ORIGIN/lib' "$out_dir/blobby-server"

# Copy the copyright files of those packages into licenses.
sort -u "$out_dir/.pkgs" | while read -r pkg; do
	mkdir -p "$out_dir/licenses/$pkg"
	cp "/usr/share/doc/$pkg/copyright" "$out_dir/licenses/$pkg/copyright" || { echo "$pkg has no copyright file" >&2; exit 1; }
	echo "license of $pkg"
done
rm -f "$out_dir/.ldd" "$out_dir/.paths" "$out_dir/.todo" "$out_dir/.seen" "$out_dir/.pkgs"

# Check that the programs now find every library in the package or on the system.
for exe in blobby blobby-server; do
	if ldd "$out_dir/$exe" | grep 'not found'; then
		echo "$exe misses a library after packaging" >&2
		exit 1
	fi
done
