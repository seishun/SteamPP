Steam++
=======

This is a C++ port of [SteamKit2](https://github.com/SteamRE/SteamKit). It's framework-agnostic – you should be able to integrate it with any event loop.

## Building

Steam++ uses [CMake](http://www.cmake.org/). If you run `cmake-gui` in the project dir, you should see which dependencies are missing. Here's the list:

### Protobuf

Used for serialization of most message types sent to/from Steam servers.  

* Debian/Ubuntu: Install `libprotobuf-dev`.
* Windows: [Download](http://code.google.com/p/protobuf/downloads) the latest source and build it yourself following the instructions provided in it and below.
* Visual Studio: Build libprotobuf.lib (Release), set `PROTOBUF_SRC_ROOT_FOLDER` to the protobuf source directory.
* MinGW: Set the install prefix to `/mingw` if you're building steampurple. Also note that you have to do this from MinGW Shell.

### Crypto++

Used for encryption.

* Debian/Ubuntu: Install `libcrypto++-dev`.
* Windows: Download and compile the [latest source](http://www.cryptopp.com/#download).
* Visual Studio: Extract into a directory named `cryptopp`. In CMake, set the following advanced variables: `CRYPTOPP_ROOT_DIR` to the parent directory of `cryptopp`, `CRYPTOPP_LIBRARY_RELEASE` to the Release build of the library, and optionally `CRYPTOPP_LIBRARY_DEBUG` to the Debug build of the library.
* MinGW: Follow the [Linux instructions](http://www.cryptopp.com/wiki/Linux#Make_and_Install) in MinGW Shell. If you are building steampurple, set PREFIX to `/mingw`.

### libarchive

Used for reading .zip archives because that's what Valve uses for data compression.

* Debian/Ubuntu: Install `libarchive-dev`.
* Windows: [Download](http://www.libarchive.org/) the latest stable release and compile it using the instructions [here](https://github.com/libarchive/libarchive/wiki/BuildInstructions) and below. Make sure to follow their instructions for zip compression support (which should involve installing zlib). In CMake, uncheck every checkbox to speed up the process.
* Visual Studio: Set the install prefix to somewhere in `CMAKE_PREFIX_PATH` (you can tweak the latter). To install, build the INSTALL project.
* MinGW: Set the install prefix to your MinGW directory if you're building steampurple. To install, run `mingw32-make install`.

### SteamKit
[SteamKit](https://github.com/SteamRE/SteamKit) repo contains .proto files we need. If you're building steampurple on MinGW, clone it into SteamPP's parent directory. Otherwise clone it wherever you want, but set the `STEAMKIT` cache variable to the directory where you cloned it.

## Usage

Steam++ is designed to be compatible with any framework – in return, you must provide it with an event loop to run in. The communication occurs through callbacks – see steam++.h and the two sample projects to get a basic idea of how it works.

## steamuv

A small project that uses [libuv](https://github.com/joyent/libuv) as the backend. You'll have to replace "username", "password" etc with real values.

### Building
1. Clone libuv somewhere and cd there
2. Build a shared library:
    - On Windows: `vcbuild.bat shared release`
    - On Linux: follow the instructions in libuv's README to clone gyp, then `./gyp_uv -Dlibrary=shared_library && make libuv -C out BUILDTYPE=Release`
3. cd into Steam++, then run CMake again, providing it the path to libuv
4. `make steamuv` should now build a `steamuv` executable

## steampurple

A libpurple plugin. Currently supports joining and leaving chats, sending and receiving friend and chat messages, as well as logging in simultaneously with the Steam client.

Note that this is very unstable and will crash at any opportunity. If it happens, please don't hesitate to submit an issue with the debug log.

### Binaries

* [Windows](http://libsteam.sf4.ru/libsteam_windows.zip)
* [64-bit Linux](http://libsteam.sf4.ru/libsteam_linux64.zip)

You can use the icons from [pidgin-opensteamworks](http://code.google.com/p/pidgin-opensteamworks/downloads/list).

### Building on Linux

1. Install development packages for libpurple and glib. On Debian/Ubuntu those are `libglib2.0-dev` and `libpurple-dev`
2. Rerun CMake
3. `make steam && cp libsteam.so ~/.purple/plugins`

### Building on MinGW

1. [Build Pidgin](https://developer.pidgin.im/wiki/BuildingWinPidgin?version=147). `pidgin-devel` should be one level above your SteamPP directory (i.e. `pidgin-devel` and `SteamPP` should be in the same folder). The "Crash Reporting Library" link is wrong in the instructions, you need [this one](https://developer.pidgin.im/static/win32/pidgin-inst-deps-20120910.tar.gz) instead. When installing MinGW, additionally check "C++ Compiler" and "MSYS Basic System".
2. Follow the [instructions above](#building) to set up the dependencies of Steam++ if you haven't yet.
3. Run the following in the SteamPP directory in MinGW Shell:
  
  ```
  cmake -G "MSYS Makefiles" -DPROTOBUF_LIBRARY=/mingw/lib/libprotobuf.a -DLibArchive_LIBRARY=/mingw/lib/libarchive_static.a -DCMAKE_PREFIX_PATH=../pidgin-devel/pidgin-2.10.7/libpurple:../pidgin-devel/win32-dev/gtk_2_0-2.14:/mingw -DCMAKE_MODULE_LINKER_FLAGS="../pidgin-devel/win32-dev/gtk_2_0-2.14/lib/libz.a -static-libgcc -static-libstdc++" -DSTEAMKIT=../SteamKit
  ```
4. Run `make steam`.
5. Copy the resulting libsteam.dll file into `%appdata%\.purple\plugins`.
