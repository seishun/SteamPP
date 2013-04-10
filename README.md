Steam++
=======

This is a C++ port of [SteamKit2](https://bitbucket.org/VoiDeD/steamre). It's framework-agnostic – you should be able to use it with any TODO: finish this sentence.

## Building

Steam++ uses CMake. If you run `cmake` (or preferably `cmake -i` or `cmake-gui`) inside the project dir, you should see which dependencies are missing. Here's the list:

* Protobuf  

Used for serialization of most message types sent to/from Steam servers.  

Debian/Ubuntu: install `libprotobuf-dev`.  
Windows: download the latest source, follow the instructions provided in it to build libprotobuf.lib (Release), then provide CMake with the path to the protobuf source directory.  

* OpenSSL

Used for encryption.  

Debian/Ubuntu: install `libssl-dev`.  
Windows: install [OpenSSL for Windows](http://slproweb.com/products/Win32OpenSSL.html) into the default installation path. "Light" versions will probably not work. 

* zlib
  
Used for CRC32. Also a dependency of libarchive.  

Debian/Ubuntu: install `libz-dev`.  
Windows: download the latest [official](http://www.zlib.net/) "compiled DLL", put the includes somewhere CMake will find them (you can tweak CMAKE_INCLUDE_PATH), put the .lib somewhere CMake will find it (you can tweak CMAKE_LIBRARY_PATH) and the .dll somewhere Windows will find it. Don't use the GnuWin32 package, it's old and broken.

* libarchive

Used to read .zip archives because that's what Valve uses for data compression. (I'm serious.)

Debian/Ubuntu: install `libarchive-dev`.  
Windows: download the latest [source](http://www.libarchive.org/) and compile it yourself using the provided instructions. Then do the same with the library, the dll and the includes as with zlib. The GnuWin32 package for libarchive is old and broken too.

Additionally, you'll need to clone [SteamRE](https://bitbucket.org/VoiDeD/steamre) somewhere and then set either the `SteamRE` environment variable or the `STEAMRE` cache variable to the directory where you cloned it.

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

A libpurple plugin. Currently supports joining chats, leaving chats, sending messages to chats and receiving messages from chats.

### Building

Building with MinGW is a pain and Windows support is not currently my highest priority. Pull requests are welcome. Linux instructions:

1. Install development packages for libpurple and glib. On Debian/Ubuntu those are `libglib2.0-dev` and `libpurple-dev`
2. Rerun CMake
3. `make steam && cp libsteam.so ~/.purple/plugins`

Note that this is very unstable and will crash at any opportunity. If it happens, please don't hesitate to submit an issue with the debug log.
