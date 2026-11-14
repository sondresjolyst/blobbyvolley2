#!/bin/sh
# Write and sign the update manifest for a release. See doc/update-manifest.md.
# Usage: make-update-manifest.sh <repo> <tag> <output dir> <packages dir>
# The packages dir holds the packages this release run built. The manifest lists their hashes,
# and the script fails if a release asset differs from the package that was built.
# The Ed25519 private key comes as PEM text in UPDATE_SIGNING_KEY.
# Writes latest.json and latest.json.sig to the output dir, and copies the root-signed
# keys.json and keys.json.sig next to them. Uploads nothing.
set -eu

repo=$1
tag=$2
out_dir=$3
packages_dir=$4

for tool in gh jq openssl sha256sum base64 od; do
	command -v "$tool" > /dev/null 2>&1 || { echo "$tool is missing" >&2; exit 1; }
done

if [ -z "${UPDATE_SIGNING_KEY:-}" ]; then
	echo "::error::UPDATE_SIGNING_KEY is empty. Add the secret to the update-signing environment, see doc/update-manifest.md." >&2
	exit 1
fi

if ! echo "$tag" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
	echo "Unexpected release tag '$tag'" >&2
	exit 1
fi
version=${tag#v}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out_dir" "$work/assets"

gh release view "$tag" --repo "$repo" --json publishedAt,assets > "$work/release.json"
released=$(jq -r '.publishedAt // empty' "$work/release.json")
if [ -z "$released" ]; then
	echo "Release $tag is not published" >&2
	exit 1
fi

# Platform name and file suffix, in the order they appear in the manifest.
: > "$work/platforms.jsonl"
for entry in windows-x64:zip macos-arm64:zip macos-x64:zip linux-x64:tar.gz; do
	platform=${entry%%:*}
	file="blobbyvolley2-tournament-$version-$platform.${entry#*:}"
	listed_size=$(jq -r --arg name "$file" '.assets[] | select(.name == $name) | .size' "$work/release.json")
	if [ -z "$listed_size" ]; then
		echo "No $platform asset on $tag, leaving it out"
		continue
	fi
	built="$packages_dir/$file"
	if [ ! -f "$built" ]; then
		echo "::error::$file is on $tag but was not built in this run" >&2
		exit 1
	fi
	gh release download "$tag" --repo "$repo" --pattern "$file" --dir "$work/assets" --clobber
	size=$(wc -c < "$built" | tr -d ' ')
	if [ "$size" != "$listed_size" ]; then
		echo "::error::$file is $size bytes but the release lists $listed_size" >&2
		exit 1
	fi
	sha256=$(sha256sum "$built" | cut -d' ' -f1)
	asset_sha256=$(sha256sum "$work/assets/$file" | cut -d' ' -f1)
	if [ "$asset_sha256" != "$sha256" ]; then
		echo "::error::The $file on $tag differs from the package built in this run" >&2
		exit 1
	fi
	jq -n -c \
		--arg name "$platform" \
		--arg url "https://github.com/$repo/releases/download/$tag/$file" \
		--argjson size "$size" \
		--arg sha256 "$sha256" \
		'{name: $name, url: $url, size: $size, sha256: $sha256}' >> "$work/platforms.jsonl"
	echo "Added $platform: $file, $size bytes, sha256 $sha256"
done

if [ ! -s "$work/platforms.jsonl" ]; then
	echo "Release $tag has no platform packages" >&2
	exit 1
fi

jq -s \
	--arg version "$version" \
	--arg tag "$tag" \
	--arg released "$released" \
	'{schema: 1, version: $version, tag: $tag, released: $released, platforms: .}' \
	"$work/platforms.jsonl" > "$out_dir/latest.json"

# Sign the exact bytes of latest.json. The .sig file is the base64 of the raw 64-byte signature.
umask 077
printf '%s\n' "$UPDATE_SIGNING_KEY" > "$work/key.pem"
openssl pkeyutl -sign -rawin -inkey "$work/key.pem" -in "$out_dir/latest.json" -out "$work/sig.bin"
openssl pkey -in "$work/key.pem" -pubout -out "$work/pub.pem"
rm -f "$work/key.pem"

sig_len=$(wc -c < "$work/sig.bin" | tr -d ' ')
if [ "$sig_len" != 64 ]; then
	echo "The signature is $sig_len bytes, not 64. Is the key Ed25519?" >&2
	exit 1
fi
openssl pkeyutl -verify -pubin -inkey "$work/pub.pem" -rawin -in "$out_dir/latest.json" -sigfile "$work/sig.bin"
base64 < "$work/sig.bin" | tr -d '\n' > "$out_dir/latest.json.sig"

# The raw public key is the last 32 bytes of the DER form. Compare it with the key in the client.
public_key=$(openssl pkey -pubin -in "$work/pub.pem" -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')
echo "Signed with public key $public_key"

# The signing key must be listed in the root-signed keys file and not revoked.
keys_dir=$(dirname "$0")/../update-keys
if [ ! -s "$keys_dir/keys.json" ] || [ ! -s "$keys_dir/keys.json.sig" ]; then
	echo "::error::.github/update-keys/keys.json or keys.json.sig is missing, see doc/update-manifest.md." >&2
	exit 1
fi
if ! jq -e --arg k "$public_key" '(.signing_keys | index($k)) != null and (.revoked | index($k)) == null' "$keys_dir/keys.json" > /dev/null; then
	echo "::error::The signing key $public_key is not an active key in keys.json." >&2
	exit 1
fi
cp "$keys_dir/keys.json" "$keys_dir/keys.json.sig" "$out_dir/"
cat "$out_dir/latest.json"
