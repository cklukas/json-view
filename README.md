# json-view

`json-view` is a small, fast JSON viewer for the terminal.  It renders
documents as an interactive tree using
[ncurses](https://invisible-island.net/ncurses/) and the
[nlohmann/json](https://github.com/nlohmann/json) library.  The viewer is
useful for quickly inspecting large files directly from the command line.

## Features

* Expand and collapse nodes with the arrow keys or the mouse.
* Search keys or values and jump between matches.
* Open multiple files or read JSON from standard input.
* Optional mouse support for selection and expansion.
* `--parse-only` mode for pretty-printing JSON without the interactive viewer.

## Requirements

* A C++20 compliant compiler
* [CMake](https://cmake.org) 3.20 or newer
* The wide-character `ncursesw` library
* [nlohmann/json](https://github.com/nlohmann/json) (bundled)

## Building

Use the provided Makefile for a simple build:

```sh
make
```

This runs CMake under the hood and places the binary in `build/json-view`.

Alternatively, invoke CMake directly:

```sh
cmake -S . -B build
cmake --build build
```

## Installation

Install the binary (defaults to `/usr/local`):

```sh
sudo cmake --install build
# or via Makefile
sudo make install
```

The install prefix can be changed at configure time using
`-DCMAKE_INSTALL_PREFIX=/usr`.

## Releases

Tagged releases on GitHub provide pre-built packages:

* Linux `.deb` and `.rpm`
* `.tar.gz` archives for Linux and macOS containing `json-view`, docs, and an `install.sh` helper

Download them from the repository's Releases page.

## Usage

```sh
json-view path/to/file1.json path/to/file2.json
# or pretty-print and exit
json-view --parse-only path/to/file.json
# or read from standard input
cat file1.json | json-view
cat file1.json | json-view --parse-only
```

If no file argument is supplied `json-view` will read a single JSON document
from standard input.  With `--parse-only` the input is formatted and printed to
standard output. Without it, the interface uses the arrow keys to navigate and
`q` to quit.

### Key bindings

* `↑/↓` – move the selection
* `→/←` – expand or collapse nodes
* `PgUp`/`PgDn` – page up/down
* `Home`/`End` – jump to first/last item
* `+` / `-` – expand all / collapse all
* `0-9` – expand to nesting level (`0` collapses all)
* `s` – search keys, `S` – search values
* `n` / `N` – next / previous search match
* `c` – clear search results
* `y` – copy selected JSON to clipboard (OSC 52 when supported)
* `?` – show a help screen
* `q` – quit the viewer

## Documentation

After installation the command line help is available through the manual and
Texinfo pages:

```sh
man json-view
info json-view
```

## License

`json-view` is released under the terms of the [GPLv3 or later License](LICENSE).
