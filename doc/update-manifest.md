# Update manifest

Each release has two extra assets: `latest.json` and `latest.json.sig`. The launcher reads them to find the newest version and the package for its platform.

The launcher fetches them from the latest release:

- `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/latest.json`
- `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/latest.json.sig`

The reusable workflow `.github/workflows/update-manifest.yaml` writes them after the packaging jobs. The release and the release repair both call it. The script is `.github/scripts/make-update-manifest.sh`.

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

Create the `update-signing` environment, limit it to `main`, and store the private key in it. Only the `sign-update-manifest` job uses this environment, and that job has no write access. A separate job uploads the files.

```sh
R=repos/sondresjolyst/blobbyvolley2-tournament
gh api --method PUT $R/environments/update-signing --input - <<< '{"deployment_branch_policy":{"protected_branches":false,"custom_branch_policies":true}}'
gh api --method POST $R/environments/update-signing/deployment-branch-policies -f name=main -f type=branch
gh secret set UPDATE_SIGNING_KEY --env update-signing --repo sondresjolyst/blobbyvolley2-tournament < update-signing-key.pem
```

Then move `update-signing-key.pem` to a password manager or other safe offline storage and delete the local copy.

If the secret is missing, the `sign-update-manifest` job fails with a message that names it. The job prints the public key it signed with, so you can compare it with the key in the launcher.

## Keys and rotation

There are two kinds of keys.

- Root keys. There are two, A and B. Their public keys are built into the launcher. Their private keys stay offline, in two separate places, and never go to GitHub.
- The signing key. It signs latest.json in CI and lives in the `UPDATE_SIGNING_KEY` secret.

`.github/update-keys/keys.json` lists the signing keys the launcher trusts and the keys that are revoked. A root key signs it into `keys.json.sig`. The release uploads both files next to latest.json.

```json
{
  "schema": 1,
  "version": 1,
  "signing_keys": ["<raw public key, hex>"],
  "revoked": []
}
```

The launcher checks an update like this:

1. Verify `keys.json.sig` against any of its built-in root keys.
2. Refuse the keys file if `version` is lower than the highest version it has seen before, and remember the new highest version.
3. Verify `latest.json.sig` with a key that is in `signing_keys` and not in `revoked`.

The release job fails if the key in `UPDATE_SIGNING_KEY` is not an active key in keys.json.

### Make the root keys

Do this once, on your own machine, outside the repo folder:

```sh
openssl genpkey -algorithm ed25519 -out update-root-a.pem
openssl genpkey -algorithm ed25519 -out update-root-b.pem
for k in update-root-a.pem update-root-b.pem; do
  openssl pkey -in "$k" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' 
' && echo
done
```

The two hex keys go into the launcher. Store A and B offline in two different places.

### Sign the keys file

```sh
sh .github/scripts/sign-update-keys.sh /path/to/update-root-a.pem
```

Commit keys.json and keys.json.sig.

### Rotate a leaked signing key

1. Make a new signing key pair as in "Key setup".
2. In keys.json, add the new public key to `signing_keys`, move the leaked key from `signing_keys` to `revoked`, and raise `version` by one.
3. Sign keys.json with a root key and commit both files.
4. Replace the `UPDATE_SIGNING_KEY` secret with the new private key.
5. Make a release. Launchers pick up the new keys file with the next update check.

### A leaked root key

Sign a new keys file with the other root key and release a launcher that drops the leaked root key and adds a new backup root key. Launchers that have not updated still trust the leaked root key until then.

## Running it locally

The script only reads the release and writes the files. It uploads nothing.

```sh
UPDATE_SIGNING_KEY="$(cat test-key.pem)" sh .github/scripts/make-update-manifest.sh sondresjolyst/blobbyvolley2-tournament v0.2.0 out
openssl pkey -in test-key.pem -pubout -out test-pub.pem
base64 -d out/latest.json.sig > sig.bin
openssl pkeyutl -verify -pubin -inkey test-pub.pem -rawin -in out/latest.json -sigfile sig.bin
```

It needs gh, jq and OpenSSL 3.

## Release repair

A platform job that does not succeed for any reason leaves its package off the release. That includes a failed job, a cancelled job and a job that never got a runner. `.github/workflows/release-repair.yaml` fixes this without manual work.

1. It runs when a Release run ends, whatever the result. A manual Release run is a dry run, so the repair skips it.
2. It checks the latest release, or the tag it was given, with `.github/scripts/release-status.sh`. Only `vX.Y.Z` tags count. It looks for the windows-x64 zip, the linux-x64 tar.gz and the macos-arm64 and macos-x64 zips. It also checks that latest.json lists exactly those packages.
3. It builds each missing package from the tag, with the same package workflows as the release, and attaches it.
4. It signs and uploads the manifest again, so latest.json matches the release.
5. It checks the release again. If something is still missing, it starts the next round. After round 3 it fails and the summary names what is missing.

The Release run and each repair round have a queue watchdog. A job that waits more than 60 minutes for a runner, while no other job is running, makes the watchdog cancel the run. The repair then takes over, so a stuck job does not hold the release for a day.

To start a repair by hand, run the Release repair workflow from main. Leave the tag empty for the latest release, or give a tag such as `v0.3.0`. Leave the round at 1.

```sh
gh workflow run release-repair.yaml --repo sondresjolyst/blobbyvolley2-tournament --ref main -f tag=v0.3.0
```

The signing job only runs from main, because the `update-signing` environment is limited to main.
