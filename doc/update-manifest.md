# Update manifest

Each release has two extra assets: `latest.json` and `latest.json.sig`. The launcher reads them to find the newest version and the package for its platform.

The launcher fetches them from the latest release:

- `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/latest.json`
- `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/latest.json.sig`

The `update-manifest` job in `.github/workflows/release.yaml` writes them after the packaging jobs. The script is `.github/scripts/make-update-manifest.sh`.

## latest.json

```json
{
  "schema": 1,
  "version": "0.2.0",
  "tag": "v0.2.0",
  "released": "2026-11-10T17:10:32Z",
  "platforms": [
    {
      "name": "windows-x64",
      "url": "https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/v0.2.0/blobbyvolley2-tournament-0.2.0-windows-x64.zip",
      "size": 11587804,
      "sha256": "57e706b53d7778ad7d12266973d74305fd88af8be4c36d7caabb7af2b4290d64"
    }
  ]
}
```

- `schema` is the format version. It is 1. A launcher that does not know the number ignores the manifest.
- `version` is the app version without the `v`.
- `tag` is the release tag.
- `released` is the time the release was published, in UTC, ISO 8601.
- `platforms` lists only the packages that are attached to the release. A platform whose build failed is missing.
- `name` is one of `windows-x64`, `macos-arm64`, `macos-x64` and `linux-x64`.
- `url` is the download link for that release. It always starts with `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/`.
- `size` is the file size in bytes.
- `sha256` is the SHA-256 of the file, as 64 lowercase hex digits.

The Windows and macOS packages are zip files. The Linux package is a tar.gz file.

New fields can be added within schema 1. Readers ignore fields they do not know. A change that breaks old readers raises the schema number.

## latest.json.sig

The signature is Ed25519 over the exact bytes of `latest.json` as downloaded. The file holds the raw 64-byte signature in standard base64 with padding. That is 88 ASCII characters with no newline. Readers should still trim whitespace around it.

To check it, the launcher:

1. Downloads both files.
2. Decodes the base64 to 64 bytes.
3. Verifies the signature over the bytes of `latest.json` with the 32-byte public key built into the launcher. With Monocypher this is `crypto_ed25519_check` from the optional `monocypher-ed25519` files. The default EdDSA functions in Monocypher use BLAKE2b and do not match.
4. Parses the JSON only after the signature is valid.
5. Checks the size and the SHA-256 of the downloaded package against the manifest before it unpacks it.

## Key setup

Do this once. Keep the private key out of the repository and off shared drives.

Make the key pair:

```sh
openssl genpkey -algorithm ed25519 -out update-signing-key.pem
```

Print the raw 32-byte public key as hex and as base64 for the launcher:

```sh
openssl pkey -in update-signing-key.pem -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n' && echo
openssl pkey -in update-signing-key.pem -pubout -outform DER | tail -c 32 | base64
```

Create the `release` environment and store the private key in it:

```sh
gh api --method PUT repos/sondresjolyst/blobbyvolley2-tournament/environments/release
gh secret set UPDATE_SIGNING_KEY --env release --repo sondresjolyst/blobbyvolley2-tournament < update-signing-key.pem
```

Then move `update-signing-key.pem` to a password manager or other safe offline storage and delete the local copy.

If the secret is missing, the `update-manifest` job fails with a message that names it. The job prints the public key it signed with, so you can compare it with the key in the launcher.

## Running it locally

The script only reads the release and writes the files. It uploads nothing.

```sh
UPDATE_SIGNING_KEY="$(cat test-key.pem)" sh .github/scripts/make-update-manifest.sh sondresjolyst/blobbyvolley2-tournament v0.2.0 out
openssl pkey -in test-key.pem -pubout -out test-pub.pem
base64 -d out/latest.json.sig > sig.bin
openssl pkeyutl -verify -pubin -inkey test-pub.pem -rawin -in out/latest.json -sigfile sig.bin
```

It needs gh, jq and OpenSSL 3.

## Reruns

If a packaging job fails and you rerun the failed jobs, GitHub reruns `update-manifest` too, so the manifest picks up the new package.
