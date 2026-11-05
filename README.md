# Blobby Volley 2 ![build workflow](https://github.com/sondresjolyst/blobbyvolley2/actions/workflows/main.yaml/badge.svg)
**The head-to-head multiplayer ball game**

### Website
 http://blobbyvolley.de

### About this fork
This repository is a fork of [danielknobe/blobbyvolley2](https://github.com/danielknobe/blobbyvolley2). To report a problem with this fork, open an issue here.

### System requirements
Windows, Linux or macOS

### Dedicated Server
The dedicated server is built as `src/blobby-server`. It runs at 100% game speed by default, which is 75 FPS.

The default port is 1234.

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
CI runs the unit tests on Linux and macOS. They do not compile on Windows yet.

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
