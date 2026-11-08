#!/bin/sh
# Lay out a self-contained macOS build from a build tree configured with BUILD_MACOS_BUNDLE=ON.
# Usage: package-macos.sh <build dir> <output dir>
# The output holds "Blobby Volley.app" and a blobby-server folder. Both carry the Homebrew
# libraries they need and get an ad-hoc signature, so they run on Apple Silicon.
# The names of the Homebrew formulae whose libraries were bundled are written to <output dir>/../formulae.txt.
set -eu

build_dir=$1
out_dir=$2

for tool in dylibbundler otool codesign brew; do
	command -v "$tool" > /dev/null 2>&1 || { echo "$tool is missing" >&2; exit 1; }
done

rm -rf "$out_dir"
mkdir -p "$out_dir"

# The install step of the bundle runs fixup_bundle, which copies the libraries into the app.
cmake --install "$build_dir" --prefix "$out_dir/.install"
app="$out_dir/Blobby Volley.app"
mv "$out_dir/.install/blobby.app" "$app"
rm -rf "$out_dir/.install"

for file in Contents/MacOS/blobby Contents/Info.plist Contents/Resources/rules.zip Contents/Resources/gfx.zip Contents/Resources/icon.icns; do
	[ -f "$app/$file" ] || { echo "the app is missing $file" >&2; exit 1; }
done

# The server gets its own folder with its libraries in lib and its data in data.
server_dir="$out_dir/blobby-server"
mkdir -p "$server_dir/data"
cp "$build_dir/src/blobby-server" "$server_dir/"
cp "$build_dir/data/rules.zip" data/server.xml data/api.lua data/rules_api.lua "$server_dir/data/"
dylibbundler -of -b -cd -x "$server_dir/blobby-server" -d "$server_dir/lib" -p @executable_path/lib/

cp COPYING README.md "$out_dir/"

# Every library reference must point into the package or to the system.
brew_prefix=$(brew --prefix)
find "$out_dir" -type f \( -perm -u+x -o -name '*.dylib' \) > "$out_dir/.machos"
: > "$out_dir/.libs"
while read -r file; do
	file "$file" | grep -q 'Mach-O' || continue
	otool -L "$file" | tail -n +2 | awk '{print $1}' > "$out_dir/.refs"
	if grep -Ev '^(/usr/lib/|/System/|@executable_path/|@loader_path/|@rpath/)' "$out_dir/.refs"; then
		echo "$file links a library outside the package" >&2
		exit 1
	fi
	case "$file" in
		*.dylib) basename "$file" >> "$out_dir/.libs" ;;
	esac
done < "$out_dir/.machos"

# Find the Homebrew formula of each bundled library and copy its license files.
: > "$out_dir/.formulae"
sort -u "$out_dir/.libs" > "$out_dir/.libs.sorted"
while read -r lib; do
	match=$(ls "$brew_prefix"/opt/*/lib/"$lib" 2>/dev/null | head -n 1 || true)
	[ -n "$match" ] || { echo "no Homebrew formula has $lib" >&2; exit 1; }
	formula=$(echo "$match" | sed "s|^$brew_prefix/opt/\([^/]*\)/lib/.*$|\1|")
	echo "bundled $lib from $formula"
	echo "$formula" >> "$out_dir/.formulae"
done < "$out_dir/.libs.sorted"
sort -u "$out_dir/.formulae" > "$out_dir/../formulae.txt"
while read -r formula; do
	keg="$brew_prefix/opt/$formula"
	mkdir -p "$out_dir/licenses/$formula"
	found=false
	for license in "$keg"/LICENSE* "$keg"/LICENCE* "$keg"/COPYING* "$keg"/share/doc/*/LICENSE* "$keg"/share/doc/*/COPYING*; do
		[ -f "$license" ] || continue
		cp "$license" "$out_dir/licenses/$formula/"
		found=true
	done
	if [ "$found" = false ]; then
		brew info "$formula" | head -n 3 > "$out_dir/licenses/$formula/LICENSE-INFO.txt"
		echo "$formula has no license file, wrote LICENSE-INFO.txt" >&2
	fi
done < "$out_dir/../formulae.txt"
rm -f "$out_dir/.machos" "$out_dir/.refs" "$out_dir/.libs" "$out_dir/.libs.sorted" "$out_dir/.formulae"

# Changing the install names broke the signatures, so sign everything again ad hoc.
for lib in "$server_dir"/lib/*.dylib; do
	codesign --force -s - "$lib"
done
codesign --force -s - "$server_dir/blobby-server"
codesign --force --deep -s - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign --verify --strict --verbose=2 "$server_dir/blobby-server"
