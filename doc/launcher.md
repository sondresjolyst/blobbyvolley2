# Launcher and updates

On Windows and Linux the game ships as a small launcher and a version folder. The launcher keeps the game up to date from the signed update manifest in `doc/update-manifest.md`. macOS keeps its app bundle without a launcher, see the last section.

## Layout

```
blobbyvolley2-tournament-0.3.0-windows-x64/
  blobby.exe                 the launcher, which players start
  *.dll, licenses/           the libraries of the launcher
  register-join-links.cmd    registers the launcher for blobby:// links
  README.md, COPYING, tournament-server.ps1
  app-0.3.0/
    blobby.exe               the game
    blobby-server.exe        the dedicated server
    data/, *.dll, licenses/
    complete.txt             written last, see below
```

Linux has the same layout with `blobby`, `lib` folders and `register-join-links.sh`. The launcher has the name the game had before, so shortcuts and the `blobby://` link handler keep working.

The settings stay in the settings folder of the game, as before. The launcher keeps its files in the `launcher` folder there.

## Starting the game

1. The launcher looks for updates, see below.
2. It lists the folders named `app-<version>` that hold `complete.txt`. A folder without that file does not count. The package build writes it last, and an update moves a whole folder into place with one rename.
3. It starts the newest version that did not fail. Before it starts a version it writes `tried-<version>`. The game writes `started-<version>` after it drew its first frame. A version with `tried` but without `started` failed, so the next start uses the version before it. When every version failed, the newest one starts.
4. It keeps the two newest versions and the one it starts, and removes the others. It renames a folder before it removes it, so a folder that is still in use on Windows stays whole.
5. It starts the game in its version folder with the same arguments, and waits for it. The game ends when the launcher is stopped or killed.

The launcher sets two variables for the game. `BLOBBY_LAUNCHER` holds the path of the launcher, so `--register-link-handler` registers the launcher and not a version folder. `BLOBBY_LAUNCHER_UPDATES=1` tells the game that the launcher can install updates.

`launcher.log` in the `launcher` folder says what the launcher did at the last start.

## Updates

At each start the launcher fetches `keys.json`, `keys.json.sig`, `latest.json` and `latest.json.sig` from `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/latest/download/`. Each file has a timeout of a few seconds, so playing offline is not delayed. It never uses api.github.com. It checks the files as `doc/update-manifest.md` says, and stores the highest keys version in `keys-version`. A damaged `keys-version` stops all updates, and the launcher points to the download page instead.

When the manifest names a newer version than every version folder, the launcher shows a small window with a progress bar. Escape or closing the window skips the update. It downloads the package only from `https://github.com/sondresjolyst/blobbyvolley2-tournament/releases/download/<tag>/`, checks the size and the SHA-256, unpacks it into a staging folder in the game folder and renames the new version folder into place. It never installs a version that is not newer. A lock file stops two launchers from updating the same folder at once.

The launcher does not replace itself. A change to the launcher reaches players when they download the game again.

When the game folder is read-only, or macOS runs the app from a temporary copy, the launcher does not install anything. It tells the player once per version and offers the download page, `https://julalan.no/last-ned`.

Set `BLOBBY_LAUNCHER_NO_UPDATE=1` to start the game without looking for updates. The package checks in CI do that.

## Update from a server

When a server says that the game is old, and the launcher can install updates, the lobby shows Update now and Not now, or Update now and Back when the server turned the game away. Update now writes `update-request` in the `launcher` folder and closes the game. The request holds the join link of the server when the game joined with a code. The launcher then looks for an update and starts the new version with that link. When there is no newer version or the update fails, the launcher says so and offers Try again, the download page and a way back to the game. The server only starts this. The launcher still takes the update from the signed manifest.

A game that was not started by such a launcher shows the download page button as before.

## Libraries

- libcurl downloads the files. Certificates are checked. On Windows they come from the Windows certificate store, because MSYS2 builds libcurl with OpenSSL and a CA file that players do not have. On Linux the launcher uses the CA bundle of the system.
- libarchive unpacks the zip and tar.gz packages. It is in MSYS2, Homebrew and apt, handles both formats and keeps the execute bit on Linux. The launcher still checks every path itself and refuses links, devices, absolute paths and `..`.
- Monocypher 4.0.2 checks the Ed25519 signatures with its optional SHA-512 files. It is in `deps/monocypher`.

## macOS

macOS gets no launcher and does not update by itself. The app bundle is signed ad hoc, and version folders or a swapped bundle could break that signature or trip Gatekeeper, which cannot be tested without a Mac. Players on macOS see the version notice of the server and the download page button, as before.
