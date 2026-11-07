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
`player1` and `player2` are player ids from the `<player>` entries. Only `id`, `player1` and `player2` are required. The others default to `default.lua`, 15 points, speed 75 and best of 1. `scoreToWin` is 1 to 99, `speed` is 30 to 120 frames per second and `bestOf` is an odd number. `ruleFile` names a file in the `rules` folder of the server. `nextGame` sets the number of the next game and `awaitingDecision="true"` holds a match until the organizer decides. `wins1` and `wins2` are the games player 1 and player 2 have won so far, 0 to 99. The server skips a match with a bad value, an unknown player or wins that already decide the series.

The site is the source of truth for the series score. When the wins of a match change in the file, the server uses them instead of its own count, also when a correction lowers them. After a restart the server starts from the wins in the file. A game the server finished counts until the file brings new wins.

Tournament mode needs a game from this fork. The game tells the server that it can play a tournament, and the server refuses an older game with the message "please update your game". Casual servers do not change, and the game still plays on older servers.

In tournament mode players cannot open or join games in the lobby. The lobby shows what each player waits for. When both players of a match are connected, the server starts the game. Player 1 plays on the left, and each player still sees their own blob on the side they picked in the options. When a game of a series ends, the next game starts 5 seconds later until a player has won more than half of the games. A player who leaves during a game ends that game as abandoned. The match then waits for the organizer until the file brings a new `nextGame` or changes `awaitingDecision` to false. By default every match starts as soon as its players are connected. To play one match at a time, set:
```xml
<var name="tournament_max_matches" value="1"/>
```
The server logs each started match and each game result, and adds them as lines of JSON to `tournament_results.jsonl`. A game line holds the report body that the game server API of the site expects. Set `tournament_results_file` to an empty value to only log them.

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
apt-get install g++ cmake libsdl2-dev libboost-dev libphysfs-dev
```
Arch-based Distros:
```bash
pacman -S gcc cmake sdl2 boost physfs
```
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
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-physfs mingw-w64-ucrt-x86_64-boost
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
brew install sdl2 physfs boost
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
