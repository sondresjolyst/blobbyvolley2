#!/bin/sh
# Remakes latest.json and latest.json.sig in this folder with .github/scripts/make-update-manifest.sh.
# It signs with the throwaway test key TestKey(4) from test/UpdateTestKeys.h and uses a stub gh,
# so it needs no network and never touches a real release. Needs jq and openssl.
# Usage: sh test/update-manifest/regenerate.sh
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$here/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The script looks for the key list next to its own folder, so run a copy with a test key list.
mkdir -p "$work/scripts" "$work/update-keys" "$work/bin" "$work/packages" "$work/out"
cp "$repo_root/.github/scripts/make-update-manifest.sh" "$work/scripts/"

# TestKey(4): seed byte i is 4 + 7 * i. The PEM is the PKCS#8 prefix for Ed25519 and the seed.
der='\060\056\002\001\000\060\005\006\003\053\145\160\004\042\004\040'
i=0
while [ $i -lt 32 ]; do
	der="$der$(printf '\\%03o' $(( (4 + 7 * i) & 255 )))"
	i=$((i + 1))
done
key_pem=$(printf "$der" | openssl base64)
UPDATE_SIGNING_KEY=$(printf -- '-----BEGIN PRIVATE KEY-----\n%s\n-----END PRIVATE KEY-----\n' "$key_pem")
export UPDATE_SIGNING_KEY
public_key=$(printf '%s\n' "$UPDATE_SIGNING_KEY" | openssl pkey -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')
printf '{"schema": 1, "version": 1, "signing_keys": ["%s"], "revoked": []}\n' "$public_key" > "$work/update-keys/keys.json"
echo "not checked by the script" > "$work/update-keys/keys.json.sig"

version=0.3.0
printf 'windows package\n' > "$work/packages/blobbyvolley2-tournament-$version-windows-x64.zip"
printf 'linux package\n' > "$work/packages/blobbyvolley2-tournament-$version-linux-x64.tar.gz"

# A stub gh that answers from the packages folder
cat > "$work/bin/gh" <<STUB
#!/bin/sh
set -eu
packages="$work/packages"
case "\$1 \$2" in
	"release view")
		printf '{"publishedAt": "2026-11-10T17:10:32Z", "assets": ['
		sep=
		for f in "\$packages"/*; do
			printf '%s{"name": "%s", "size": %s}' "\$sep" "\$(basename "\$f")" "\$(wc -c < "\$f" | tr -d ' ')"
			sep=,
		done
		printf ']}\n'
		;;
	"release download")
		shift 2
		pattern= dir=
		while [ \$# -gt 0 ]; do
			case "\$1" in
				--pattern) pattern=\$2; shift ;;
				--dir) dir=\$2; shift ;;
			esac
			shift
		done
		cp "\$packages/\$pattern" "\$dir/"
		;;
	*) echo "unexpected gh call: \$*" >&2; exit 1 ;;
esac
STUB
chmod +x "$work/bin/gh"

# Git Bash needs a /c/... form in PATH, since the colon of C:/... splits it
stub_bin=$work/bin
if command -v cygpath > /dev/null 2>&1; then
	stub_bin=$(cygpath -u "$stub_bin")
	# A native Windows jq writes CRLF unless it gets --binary. CI writes LF.
	printf '#!/bin/sh\nexec "%s" --binary "$@"\n' "$(command -v jq)" > "$work/bin/jq"
	chmod +x "$work/bin/jq"
fi
PATH="$stub_bin:$PATH"
if [ "$(command -v gh)" != "$stub_bin/gh" ]; then
	echo "The stub gh is not first on PATH" >&2
	exit 1
fi
sh "$work/scripts/make-update-manifest.sh" \
	sondresjolyst/blobbyvolley2-tournament "v$version" "$work/out" "$work/packages"
cp "$work/out/latest.json" "$work/out/latest.json.sig" "$here/"
