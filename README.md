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

```sh
cmake -S . -B build
cmake --build build
```

## Installation

Install the binary (defaults to `/usr/local`):

```sh
sudo cmake --install build
```

The install prefix can be changed at configure time using
`-DCMAKE_INSTALL_PREFIX=/usr`.

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
