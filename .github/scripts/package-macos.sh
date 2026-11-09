#!/bin/sh
# Lay out a self-contained macOS build from a build tree configured with BUILD_MACOS_BUNDLE=ON.
# Usage: package-macos.sh <build dir> <output dir>
# The output holds "Blobby Volley 2 Tournament.app" and a blobby-server folder. Both carry the Homebrew
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
# fixup_bundle needs an absolute path.
out_dir=$(cd "$out_dir" && pwd -P)
brew_prefix=$(brew --prefix)

# The install step of the bundle runs fixup_bundle, which copies the libraries into the app.
cmake --install "$build_dir" --prefix "$out_dir/.install"
app="$out_dir/Blobby Volley 2 Tournament.app"
mv "$out_dir/.install/blobby.app" "$app"
rm -rf "$out_dir/.install"

for file in Contents/MacOS/blobby Contents/Info.plist Contents/Resources/rules.zip Contents/Resources/gfx.zip Contents/Resources/icon.icns; do
	[ -f "$app/$file" ] || { echo "the app is missing $file" >&2; exit 1; }
done

# The app must not share its bundle id with the original game, so both can be installed.
bundle_id=$(plutil -extract CFBundleIdentifier raw "$app/Contents/Info.plist")
[ "$bundle_id" = "no.sjolystinnovation.blobbyvolley2-tournament" ] || { echo "the app has the bundle id $bundle_id" >&2; exit 1; }

# The server gets its own folder with its libraries in lib and its data in data.
server_dir="$out_dir/blobby-server"
mkdir -p "$server_dir/data"
cp "$build_dir/src/blobby-server" "$server_dir/"
cp "$build_dir/data/rules.zip" "$build_dir/data/scripts.zip" data/server.xml data/api.lua data/bot_api.lua data/rules_api.lua "$server_dir/data/"
dylibbundler -of -b -cd -x "$server_dir/blobby-server" -d "$server_dir/lib" -p @executable_path/lib/

# Homebrew's SDL2 is sdl2-compat, which loads SDL3 at run time from the folder it is in.
# Neither fixup_bundle nor dylibbundler sees that, so copy SDL3 next to each copy of SDL2,
# under the name that this build of sdl2-compat asks for.
if [ -e "$brew_prefix/opt/sdl2-compat" ]; then
	sdl3_name=$(strings -a "$app/Contents/Frameworks/libSDL2-2.0.0.dylib" | sed -n 's|^@loader_path/\(libSDL3[^/]*\.dylib\)$|\1|p' | head -n 1)
	[ -n "$sdl3_name" ] || { echo "sdl2-compat does not look for SDL3 next to itself" >&2; exit 1; }
	echo "sdl2-compat opens @loader_path/$sdl3_name"
	for dir in "$app/Contents/Frameworks" "$server_dir/lib"; do
		[ -f "$dir/libSDL2-2.0.0.dylib" ] || { echo "no libSDL2-2.0.0.dylib in $dir" >&2; exit 1; }
		cp -L "$brew_prefix/opt/sdl3/lib/libSDL3.dylib" "$dir/$sdl3_name"
		chmod 644 "$dir/$sdl3_name"
		install_name_tool -id "@loader_path/$sdl3_name" "$dir/$sdl3_name"
	done
fi

cp COPYING README.md "$out_dir/"

# Turn @executable_path and @loader_path in a path into real folders.
# Usage: expand <path> <executable folder> <loader folder>
expand() {
	echo "$1" | sed -e "s|^@executable_path|$2|" -e "s|^@loader_path|$3|"
}
# Print the path if it is a file inside the package.
inside() {
	[ -f "$1" ] || return 1
	real=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
	case "$real" in
		"$out_dir"/*) echo "$real" ;;
		*) return 1 ;;
	esac
}

# Every library reference must point to the system or to a file inside the package,
# also through @rpath, so a library that was left in Homebrew fails here.
find "$out_dir" -type f \( -perm -u+x -o -name '*.dylib' \) > "$out_dir/.machos"
: > "$out_dir/.libs"
while read -r file; do
	file "$file" | grep -q 'Mach-O' || continue
	case "$file" in
		"$app"/*) exe_dir="$app/Contents/MacOS" ;;
		*) exe_dir="$server_dir" ;;
	esac
	loader_dir=$(dirname "$file")
	# Drop run paths that lead out of the package, such as the Homebrew folder of SDL3.
	otool -l "$file" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' > "$out_dir/.rpaths"
	: > "$out_dir/.rpaths.kept"
	while read -r rpath; do
		dir=$(expand "$rpath" "$exe_dir" "$loader_dir")
		if [ -d "$dir" ] && case "$(cd "$dir" && pwd -P)" in "$out_dir"*) true ;; *) false ;; esac; then
			echo "$rpath" >> "$out_dir/.rpaths.kept"
		else
			echo "removing run path $rpath from $file"
			install_name_tool -delete_rpath "$rpath" "$file"
		fi
	done < "$out_dir/.rpaths"
	otool -L "$file" | tail -n +2 | awk '{print $1}' > "$out_dir/.refs"
	while read -r ref; do
		case "$ref" in
			/usr/lib/*|/System/*) continue ;;
			@executable_path/*|@loader_path/*)
				inside "$(expand "$ref" "$exe_dir" "$loader_dir")" > /dev/null || { echo "$file links $ref, which is not in the package" >&2; exit 1; } ;;
			@rpath/*)
				found=false
				# A library also sees the run paths of the program that loads it.
				cat "$out_dir/.rpaths.kept" > "$out_dir/.search"
				[ "$file" = "$exe_dir/blobby" ] || [ "$file" = "$exe_dir/blobby-server" ] || \
					otool -l "$exe_dir/blobby" "$exe_dir/blobby-server" 2>/dev/null | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' >> "$out_dir/.search"
				while read -r rpath; do
					if inside "$(expand "$rpath" "$exe_dir" "$loader_dir")/${ref#@rpath/}" > /dev/null; then
						found=true
						break
					fi
				done < "$out_dir/.search"
				[ "$found" = true ] || { echo "$file links $ref, which no run path finds in the package" >&2; exit 1; } ;;
			*)
				echo "$file links $ref, which is outside the package" >&2
				exit 1 ;;
		esac
	done < "$out_dir/.refs"
	case "$file" in
		*.dylib) basename "$file" >> "$out_dir/.libs" ;;
	esac
done < "$out_dir/.machos"

# Find the Homebrew formula of each bundled library. Write its license id and copy its license
# texts, as the Windows package does. A library without both fails the build.
: > "$out_dir/.formulae"
sort -u "$out_dir/.libs" > "$out_dir/.libs.sorted"
while read -r lib; do
	match=$(ls "$brew_prefix"/opt/*/lib/"$lib" 2>/dev/null | head -n 1 || true)
	[ -n "$match" ] || { echo "no Homebrew formula has $lib" >&2; exit 1; }
	formula=$(echo "$match" | sed "s|^$brew_prefix/opt/\([^/]*\)/lib/.*$|\1|")
	formula=$(brew info --json=v2 "$formula" | python3 -c 'import json, sys; print(json.load(sys.stdin)["formulae"][0]["name"])')
	echo "bundled $lib from $formula"
	echo "$formula $lib" >> "$out_dir/.formulae"
done < "$out_dir/.libs.sorted"
cut -d' ' -f1 "$out_dir/.formulae" | sort -u > "$out_dir/../formulae.txt"
while read -r formula; do
	keg="$brew_prefix/opt/$formula"
	target="$out_dir/licenses/$formula"
	mkdir -p "$target"
	brew info --json=v2 "$formula" | python3 -c '
import json, sys
f = json.load(sys.stdin)["formulae"][0]
if not f.get("license"):
    sys.exit("no license id for " + f["name"])
print("Name: " + f["name"])
print("Version: " + f["versions"]["stable"])
print("License: " + f["license"])
print("Homepage: " + f["homepage"])
' > "$target/LICENSE-INFO.txt"
	echo "Libraries: $(awk -v f="$formula" '$1 == f { print $2 }' "$out_dir/.formulae" | tr '\n' ' ')" >> "$target/LICENSE-INFO.txt"
	found=false
	for license in "$keg"/LICENSE* "$keg"/LICENCE* "$keg"/COPYING* "$keg"/share/doc/*/LICENSE* "$keg"/share/doc/*/COPYING*; do
		[ -f "$license" ] || continue
		cp "$license" "$target/"
		found=true
	done
	[ "$found" = true ] || { echo "$formula has no license text in $keg" >&2; exit 1; }
	echo "license of $formula: $(sed -n 's/^License: //p' "$target/LICENSE-INFO.txt")"
done < "$out_dir/../formulae.txt"
rm -f "$out_dir/.machos" "$out_dir/.refs" "$out_dir/.rpaths" "$out_dir/.rpaths.kept" "$out_dir/.search" "$out_dir/.libs" "$out_dir/.libs.sorted" "$out_dir/.formulae"
echo "bundled libraries:"
(cd "$out_dir" && find . -name '*.dylib' | sort)

# Changing the install names broke the signatures, so sign everything again ad hoc.
for lib in "$server_dir"/lib/*.dylib; do
	codesign --force -s - "$lib"
done
codesign --force -s - "$server_dir/blobby-server"
codesign --force --deep -s - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign --verify --strict --verbose=2 "$server_dir/blobby-server"
