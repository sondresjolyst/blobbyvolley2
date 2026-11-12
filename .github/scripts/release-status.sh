#!/bin/sh
# Check that a release has every platform package and an update manifest that lists them.
# Usage: release-status.sh <repo> <tag>
# Prints two lines in the GITHUB_OUTPUT format:
#   missing=<platforms without a package, separated by spaces>
#   manifest_ok=<true when latest.json, its signature and the keys files are on the release
#               and latest.json lists exactly the packages on it, with the same sizes>
# Reads the release only. Uploads nothing.
set -eu

repo=$1
tag=$2

for tool in gh jq; do
	command -v "$tool" > /dev/null 2>&1 || { echo "$tool is missing" >&2; exit 1; }
done

if ! echo "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
	echo "Unexpected release tag '$tag'" >&2
	exit 1
fi
version=${tag#v}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

gh release view "$tag" --repo "$repo" --json isDraft,publishedAt,assets > "$work/release.json"
if [ "$(jq -r '.isDraft' "$work/release.json")" = true ] || [ -z "$(jq -r '.publishedAt // empty' "$work/release.json")" ]; then
	echo "Release $tag is not published" >&2
	exit 1
fi

# Size of an uploaded asset, or nothing.
asset_size() {
	jq -r --arg name "$1" '.assets[] | select(.name == $name and .state == "uploaded") | .size' "$work/release.json" | tr -d '\r'
}

missing=""
: > "$work/present"
for entry in windows-x64:zip linux-x64:tar.gz macos-arm64:zip macos-x64:zip; do
	platform=${entry%%:*}
	file="blobbyvolley2-tournament-$version-$platform.${entry#*:}"
	size=$(asset_size "$file")
	if [ -z "$size" ]; then
		echo "$tag has no $file" >&2
		missing="$missing $platform"
	else
		echo "$tag has $file, $size bytes" >&2
		echo "$platform:$size" >> "$work/present"
	fi
done

manifest_ok=true
for file in latest.json latest.json.sig keys.json keys.json.sig; do
	if [ -z "$(asset_size "$file")" ]; then
		echo "$tag has no $file" >&2
		manifest_ok=false
	fi
done
if [ "$manifest_ok" = true ]; then
	gh release download "$tag" --repo "$repo" --pattern latest.json --dir "$work" --clobber
	jq -r --arg tag "$tag" 'select(.tag == $tag) | .platforms[] | "\(.name):\(.size)"' "$work/latest.json" | tr -d '\r' | sort > "$work/listed"
	tr -d '\r' < "$work/present" | sort > "$work/expected"
	if ! cmp -s "$work/listed" "$work/expected"; then
		echo "latest.json on $tag does not match the packages on it" >&2
		manifest_ok=false
	fi
fi

echo "missing=${missing# }"
echo "manifest_ok=$manifest_ok"
