#!/bin/sh
# Sign .github/update-keys/keys.json with an offline root key. See doc/update-manifest.md.
# Usage: sign-update-keys.sh <root key PEM file>
# Run it on your own machine. The root key never goes to GitHub.
set -eu

root_key=$1
dir=$(dirname "$0")/../update-keys
keys="$dir/keys.json"

jq -e '.schema == 1 and (.version | type == "number") and (.signing_keys | type == "array") and (.revoked | type == "array")' "$keys" > /dev/null || {
	echo "$keys is not a valid keys file" >&2
	exit 1
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
openssl pkeyutl -sign -rawin -inkey "$root_key" -in "$keys" -out "$work/sig.bin"
openssl pkey -in "$root_key" -pubout -out "$work/pub.pem"
openssl pkeyutl -verify -pubin -inkey "$work/pub.pem" -rawin -in "$keys" -sigfile "$work/sig.bin"
base64 < "$work/sig.bin" | tr -d '\n' > "$keys.sig"
echo "Signed $keys with root key $(openssl pkey -pubin -in "$work/pub.pem" -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')"
