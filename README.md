# Blobby Volley 2 ![build workflow](https://github.com/sondresjolyst/blobbyvolley2/actions/workflows/main.yaml/badge.svg)
**The head-to-head multiplayer ball game**

### Website
 http://blobbyvolley.de

### About this fork
This repository is a fork of [danielknobe/blobbyvolley2](https://github.com/danielknobe/blobbyvolley2). To report a problem with this fork, open an issue [here](https://github.com/sondresjolyst/blobbyvolley2/issues).

### System requirements
Windows, Linux or macOS

### Sponsor board
The game can show a scrolling LED board with sponsor logos behind the net during a match. It shows when "show sponsors" is on in the graphic options and at least one logo is found. The option is on by default. The game ships no logos.

Put the logos in a folder named `sponsors`. The game reads it from the `data` folder and from the user folder, which on Windows is `%APPDATA%\org.dk\blobby`. The game creates an empty `sponsors` folder there on start.

Each logo must be a BMP file up to 160 pixels wide and 40 pixels high. 32 pixels high looks best. Opaque logos can be uncompressed 24-bit BMPs. Logos with transparency must be 32-bit BMPs with an alpha channel, saved with a V4 or V5 header and BI_BITFIELDS, so the board shows through. The logos scroll in file name order. Files that do not match are skipped with a warning.

### Dedicated Server
The dedicated server is built as `src/blobby-server`. It runs at 100% game speed by default, which is 75 FPS.

The default port is 1234.

#### Run the server in Docker
A container image of the dedicated server is published on Docker Hub as `sondresjo/blobby-server`. The `latest` tag is the newest release. The `dev` tag follows the main branch. On Windows, start Docker Desktop and run:
```bash
docker run -d --name blobby-server -p 1234:1234/udp sondresjo/blobby-server
```
The server log is shown with `docker logs blobby-server`.

To use your own config, create a folder named `config` and put a copy of `data/server.xml` in it as `custom.xml`. Then mount the folder and pass the file name with `-c`. The file name must match, because the server looks for it in the mounted folder. In a Windows command prompt:
```bash
docker run -d --name blobby-server -p 1234:1234/udp -v "%cd%\config:/usr/local/share/blobby/server:ro" sondresjo/blobby-server -c custom.xml
```
Players on other machines may need a Windows firewall rule that allows inbound UDP on port 1234.

To run a tournament server for the tournament site, pass the settings from the admin page of the site and keep `/data` on a volume:
```bash
docker run -d --name blobby-server --restart unless-stopped -p 1234:1234/udp -v blobby-data:/data -e BLOBBY_TOURNAMENT=1 -e BLOBBY_SITE_URL=https://<site> -e BLOBBY_SITE_TOKEN=<token> sondresjo/blobby-server
```
See [Connect to the tournament site](#connect-to-the-tournament-site).

To build the image yourself, run `docker build -t blobby-server .` in the repository root.

#### Tournament mode
In tournament mode each player types a join code from the tournament site as player name. The server swaps the code for the nickname of that player and refuses an unknown code or a code that another connected player already uses. The client then shows why it was refused. Tournament mode is off by default. To turn it on, set these values in the server config:
```xml
<var name="tournament_mode" value="true"/>
<var name="tournament_players_file" value="tournament_players.xml"/>
```
A relative path starts in the working directory of the server. The players file looks like this:
```xml
<?xml version="1.0" encoding="utf-8"?>
<tournament>
	<player id="12" nickname="Ola" code="K7QM2XPR4T"/>
	<player id="17" nickname="Kari" code="B9HNZ3WD6F"/>
</tournament>
```
A code has 10 characters from `ABCDEFGHJKMNPQRSTUVWXYZ23456789`. Players may type it in lower case. A nickname is 1 to 15 bytes of UTF-8. The `id` is the player id on the site and may be left out. The server skips an entry with a bad code or nickname, and it skips every entry of a code or an `id` that appears more than once. Other elements below `<tournament>` are ignored.

The server reads the file again within a few seconds after it changes. If the file is missing or is not valid XML, the server keeps the players it read last and logs an error. Write a new file under another name and rename it over the old one, so the server never reads half a file. The log shows only the first 3 characters of a code.

The same file assigns matches. The fields have the names and meanings of a match in the sync of the tournament site:
```xml
<match id="7" player1="12" player2="17" ruleFile="default.lua" scoreToWin="15" speed="75" bestOf="3"/>
```
`player1` and `player2` are player ids from the `<player>` entries. Only `id`, `player1` and `player2` are required. The others default to `default.lua`, 15 points, speed 75 and best of 1. `scoreToWin` is 1 to 99, `speed` is 30 to 120 frames per second and `bestOf` is an odd number. `ruleFile` names a file in the `rules` folder of the server. `nextGame` sets the number of the next game and `awaitingDecision="true"` holds a match until the organizer decides. `wins1` and `wins2` are the games player 1 and player 2 have won so far, 0 to 99. `nextGameDelay` is the pause between two games of the series, 3 to 120 seconds. The server skips a match with a bad value, an unknown player or wins that already decide the series.

The site is the source of truth for the series score. When the wins of a match change in the file, the server uses them instead of its own count, also when a correction lowers them. After a restart the server starts from the wins in the file. A game the server finished counts until the file brings new wins.

Tournament mode needs a game from this fork. The game tells the server that it can play a tournament, and the server refuses an older game with the message "please update your game". Casual servers do not change, and the game still plays on older servers.

In tournament mode players cannot open or join games in the lobby. The lobby shows what each player waits for. When both players of a match are connected, the server starts the game. Player 1 plays on the left, and each player still sees their own blob on the side they picked in the options. When a game of a series ends, the win screen counts down to the next game. Games go on until a player has won more than half of them. The pause is 10 seconds unless `tournament_next_game_delay` in the server config sets 3 to 120 seconds, and `nextGameDelay` on a `<match>` sets it for that match. After the last game, OK takes the player back to the tournament lobby without leaving the server. A player who leaves during a game ends that game as abandoned, and the report names that player in `leftPlayerId`. The lobby then says that the game stopped and that an organizer decides how it continues. The match then waits for the organizer until the file brings a new `nextGame` or changes `awaitingDecision` to false. By default every match starts as soon as its players are connected. To play one match at a time, set:
```xml
<var name="tournament_max_matches" value="1"/>
```
The server logs each started match and each game result, and adds them as lines of JSON to `tournament_results.jsonl`. A game line holds the report body that the game server API of the site expects. Set `tournament_results_file` to an empty value to only log them.

##### Connect to the tournament site
With the address and the token of the tournament site, the server reads the players file from the site and reports each game to it. The settings are environment variables, which come before the config file:
- `BLOBBY_TOURNAMENT=1` turns tournament mode on, and `0` turns it off.
- `BLOBBY_SITE_URL` is the address of the site. It must start with `https://`.
- `BLOBBY_SITE_TOKEN` is the game server token from the admin page of the site.
- `BLOBBY_DATA_DIR` is the folder for the files below. The container image uses `/data`.

The config file can set the same as `tournament_mode`, `tournament_site_url`, `tournament_site_token` and `tournament_data_dir`. The server does not start when the address is not https or the token is missing. It never logs the token.

The server reads the sync of the site every 3 seconds. When it changed, the server writes it as `tournament_players.xml` in the data folder, in the format above. Each report goes to `tournament_outbox.jsonl` before it is sent. The server sends the reports of a match one at a time in game order and removes each one when the site answers it. While the site cannot be reached, matches keep running and the reports wait in the outbox, also across a restart of the server. The server tries again after 1 second, then waits twice as long each time, up to 30 seconds. Until the site has a game, the players file counts it, so a restart does not play it again. The last sync is kept in `tournament_sync.json` for that. A report the site refuses for good is logged and added to `tournament_refused.jsonl`. With a wrong token the reports wait until the token is fixed.

The score of each running game goes to the site at most once a second, for the big screen. It is never queued.

The server also reads the sponsors of the site when they change and keeps them in the `sponsors` folder of the data folder. `board.json` holds the board style and the sponsors in order, and the `logos` folder holds each logo as `<hash>.bmp`. The game does not show these yet.

For a test site, `BLOBBY_SITE_CA_FILE` adds certificates to trust, and `BLOBBY_SITE_ALLOW_HTTP=1` allows plain http to localhost. Do not set them at the LAN.

The site client needs libcurl and nlohmann_json at build time. Without them the server is built without it, and it does not start with `BLOBBY_SITE_URL` set.

#### Join links
The tournament site shows each player a join link of the form `blobby://join?server=<host>:<port>&code=<code>`. The link takes the game straight to the tournament server with your join code. It works the same on Windows and Linux.

To join a match:
1. Copy your join link on the tournament site.
2. Open Blobby Volley.
3. Click "join link" at the top of the main menu.
4. Answer Yes when the game asks whether to join the server.

The game then connects and sends the code as player name. If the clipboard holds no join link, or the link is not valid, the game says so and stays in the main menu.

The game checks the link strictly before it uses it. The host is a host name or an IPv4 address of at most 63 bytes. A host that starts with a digit must be an IPv4 address of four decimal parts. The port is 1 to 65535 and the code has 10 characters from the join code alphabet. The link may hold only `server` and `code`. The game never opens or runs anything else from a link.

##### Open links with a click (optional)
You can also let a click on the link open the game. The game then asks the same question before it joins. Each click starts a new copy of the game, even when one is already running. A link given on the command line must be the only argument. A bad link shows a message, and the game opens the main menu.

On Windows, run `register-join-links.cmd` from the game folder once. It writes the handler for the current user below `HKEY_CURRENT_USER\Software\Classes\blobby` and needs no admin rights. Run it again after you move the game folder. `unregister-join-links.cmd` removes the handler. The two files call `blobby.exe --register-link-handler` and `blobby.exe --unregister-link-handler`.

On Linux, run `linux/register-join-links.sh` once, with the path of the game as argument if `blobby` is not on your `PATH`:
```bash
sh linux/register-join-links.sh /path/to/blobby
```
It writes `~/.local/share/applications/blobby-join.desktop` for the scheme `x-scheme-handler/blobby` and makes it the default with `xdg-mime`. It needs no root rights. Add `--print` to see the file without writing it. `linux/unregister-join-links.sh` removes the handler.

### Source Code
Clone the git repository:
```bash
git clone https://github.com/sondresjolyst/blobbyvolley2.git
```

### Build under Linux
Blobby Volley compiles with GCC 9 or newer, or Clang 12 or newer.
Other compilers may work but are currently untested. The minimum supported
CMake version is 3.11.

1. Install dependencies:

Debian-based Distros:
```bash
apt-get install g++ cmake libsdl2-dev libboost-dev libphysfs-dev libcurl4-openssl-dev nlohmann-json3-dev
```
Arch-based Distros:
```bash
pacman -S gcc cmake sdl2 boost physfs curl nlohmann-json
```
libcurl and nlohmann_json are only needed for the tournament site client of the server.
2. Compile:
```bash
cmake .
make
```
3. Run:
```bash
src/blobby
```

### Build under Windows with MSYS2
1. Install MSYS2 from https://www.msys2.org.

2. Open the **MSYS2 UCRT64** shell from the Start menu.

3. Install dependencies:
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-physfs mingw-w64-ucrt-x86_64-boost mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-nlohmann-json
```
4. Compile in the project directory:
```bash
cmake .
cmake --build .
```
5. Run:
```bash
src/blobby.exe
```

The game needs the MSYS2 DLLs, so outside the UCRT64 shell it does not start until they are on `PATH`.
In Git Bash, with MSYS2 in its default folder, run this first:
```bash
export PATH="$PATH:/c/msys64/ucrt64/bin"
```

### Download for Windows
Every build of `main` and every pull request uploads a Windows package as an artifact named `blobby-windows-<short sha>`.
Open the workflow run on the Actions page of the fork, download the artifact and unzip it.
Double-click `blobby.exe` to play. `blobby-server.exe` starts a dedicated server.
The package has all the DLLs it needs, so MSYS2 does not have to be installed.
Start the game from its own folder, as Explorer and shortcuts do.
To join a tournament match, copy your join link and click "join link" at the top of the main menu. See [Join links](#join-links).

### Build under Windows with Visual Studio and vcpkg
1. Install Visual Studio with the **Desktop development with C++** workload.

2. Install vcpkg by following the instructions:
https://github.com/microsoft/vcpkg/blob/master/README.md

3. Configure and build in the project directory. vcpkg installs the dependencies listed in `vcpkg.json`. Replace `C:/vcpkg` with your vcpkg folder:
```powershell
cmake -B . -S . -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build .
```

For more about using vcpkg with CMake, see:
https://learn.microsoft.com/vcpkg/users/buildsystems/cmake-integration

### Build under macOS
1. Install homebrew by following the instructions:
https://brew.sh

2. Install dependencies:
```bash
brew install sdl2 physfs boost nlohmann-json
```
3. Compile:
```bash
cmake .
make
```
4. Run:
```bash
src/blobby
```

### Build for Nintendo Switch

1. Install and configure [devkitPro](https://switchbrew.org/wiki/Setting_up_Development_Environment).

2. Install dependencies through devkitPro's package manager:

- SDL2
- PhysFS
- OpenGL

3. Compile:

```bash
cmake . -DCMAKE_TOOLCHAIN_FILE=NintendoSwitchToolchain.cmake -DCMAKE_INSTALL_PREFIX=blobby -DSWITCH=true -DCMAKE_BUILD_TYPE=Debug
make && make install
```

4. Copy the `blobby` folder to the `/switch/` directory on your SD card.

### Run the unit tests
CI runs the unit tests on Linux, macOS and Windows.

On Debian-based distros, also install `libboost-test-dev`. Then run:
```bash
cmake . -DBUILD_TESTS=ON
cmake --build .
test/blobbytest
```

### Credits
See AUTHORS

### License
GPL version 2. See COPYING.
