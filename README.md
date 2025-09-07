# json-view

A small terminal JSON viewer using [ncurses](https://invisible-island.net/ncurses/) and [nlohmann/json](https://github.com/nlohmann/json).

## Build

This project uses [CMake](https://cmake.org) and requires a C++20 compiler and the wide-character ncurses library.

```sh
cmake -S . -B build
cmake --build build
```

## Install

To install the `json-view` binary into `/usr/local/bin` (the default prefix on macOS and most Linux systems):

```sh
sudo cmake --install build
```

You can choose a different prefix by setting `-DCMAKE_INSTALL_PREFIX=/usr` when configuring CMake.

## Usage

```
json-view path/to/file.json
```

The viewer uses the arrow keys to navigate and `q` to quit.
