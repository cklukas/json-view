# json-view

`json-view` is a small, fast JSON viewer for the terminal.  It renders
documents as an interactive tree using
[ncurses](https://invisible-island.net/ncurses/) and the
[nlohmann/json](https://github.com/nlohmann/json) library.  The viewer is
useful for quickly inspecting large files directly from the command line.

The repository also provides `json-view-app`, a
[Turbo Vision](https://github.com/magiblot/tvision) based TUI that offers
similar navigation and search capabilities through a classic menu driven
interface. The program accepts file names on the command line for
immediate viewing and includes a Help→About dialog displaying the current
version and developer information.

json-view:

```
▼ /tmp/test.json (📦 dictionary, 7 keys, 951 Bytes)
├── ☒ active: true
├── ▼ deeply_nested (dictionary, 1 key)
│   └── ▶ level1 (dictionary, 1 key)
├── ▼ items (list, 8 items)
│   ├── ⊘ [0]: null
│   ├── ☐ [1]: false
│   ├── ☒ [2]: true
│   ├── ⅑ [3]: 0
│   ├── ⅑ [4]: -123.456
│   ├── ℀ [5]: "a string with \"quotes\" and unicode ✓"
│   ├── ▶ [6] (list, 4 items): 1, 2, 3, {...}
│   └── ▶ [7] (dictionary, 2 keys)
├── ▼ metadata (dictionary, 3 keys)
│   ├── ▶ contributors (list, 2 items): {...}, {...}
│   ├── ℀ description: "Nested structures with various types"
│   └── ▶ tags (list, 3 items): "json", "test", "viewer"
├── ℀ name: "Sample Dataset"
├── ▼ stats (dictionary, 3 keys)
│   ├── ⅑ count: 42
│   ├── ▶ invalid_values (dictionary, 3 keys)
│   └── ⅑ ratio: 0.61803398875
└── ⅑ version: 1



test.json   (?:help, q:quit)
```

json-view help screen:

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│  JSON Viewer Key Bindings:                                                        │
│                                                                                   │
│    ↑/↓              Move selection up or down                                     │
│    PgUp/PgDn        Move one page up or down                                      │
│    Home/End         Jump to first or last item                                    │
│    ←                Collapse the current item or go to its parent                 │
│    →                Expand the current item                                       │
│    +                Expand all items                                              │
│    -                Collapse all items                                            │
│    0-9              Expand to nesting level (0=collapse all, 1=first level, etc.) │
│    s                Search keys                                                   │
│    S                Search values                                                 │
│    n / N            Next / previous search match                                  │
│    c                Clear search results                                          │
│    t                Cycle color scheme                                            │
│    y                Copy selected JSON to clipboard                               │
│    ?                Show this help screen                                         │
│    q                Quit the program                                              │
│                                                                                   │
│  Press any key to return...                                                       │
└───────────────────────────────────────────────────────────────────────────────────┘
```

json-view-app:

```
  File  Edit  Search  View  Help                                                                                    
 ┌─────────────┐ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
 │ Open     F2 │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
 │ Close    F4 │ ═══════════════════════════════════ test.json ══════════════════════════════════════════════[↑]═╗░░
 ├─────────────┤ st.json (dictionary, 7 keys, 951 Bytes)                                                         ║░░
 │ Exit  Alt-X │ true                                                                                            ║░░
 └─────────────┘ ested (dictionary, 1 key)                                                                       ║░░
░░║   ├──items (list, 8 items)                                                                                   ║░░
░░║   │  ├──[0]: null                                                                                            ║░░
░░║   │  ├──[1]: false                                                                                           ║░░
░░║   │  ├──[2]: true                                                                                            ║░░
░░║   │  ├──[3]: 0                                                                                               ║░░
░░║   │  ├──[4]: -123.456                                                                                        ║░░
░░║   │  ├──[5]: "a string with \"quotes\" and unicode ✓"                                                        ║░░
░░║   │  ├─+[6] (list, 4 items)                                                                                  ║░░
░░║   │  └─+[7] (dictionary, 2 keys)                                                                             ║░░
░░║   ├──metadata (dictionary, 3 keys)                                                                           ║░░
░░║   │  ├──contributors (list, 2 items)                                                                         ║░░
░░║   │  │  ├──[0] (dictionary, 3 keys)                                                                          ║░░
░░║   │  │  │  ├──active: true                                                                                   ║░░
░░║   │  │  │  ├──name: "Alice"                                                                                  ║░░
░░║   │  │  │  └─+roles (list, 2 items)                                                                          ║░░
░░║   │  │  └──[1] (dictionary, 3 keys)                                                                          ║░░
░░║   │  │     ├──active: false                                                                                  ║░░
░░║   │  │     ├──name: "Bob"                                                                                    ║░░
░░║   │  │     └─+roles (list, 1 item)                                                                           ║░░
░░║   │  ├──description: "Nested structures with various types"                                                  ║░░
░░║   │  └─+tags (list, 3 items)                                                                                 ║░░
░░║   ├──name: "Sample Dataset"                                                                                  ║░░
░░║   ├─+stats (dictionary, 3 keys)                                                                              ║░░
░░║   └──version: 1                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░║                                                                                                              ║░░
░░└─════════════════════════════════════════════════════════════════════════════════════════════════════════════─┘░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
 F2 Open  F3 Find  Ctrl+Q Quit                                                                                      
```

## macOS Configuration for `json-view-app`

A quick note for macOS users. The `json-view-app` uses `Alt` key combinations for menu access and shortcuts (e.g., `Alt+F` for the File menu), which is a common convention in terminal applications on Linux and Windows.

### The Issue: `Option` vs. `Alt`

On macOS, the `Option` key (⌥) has a different primary purpose: it's used to type special characters and accents (e.g., `Option+P` types `π`). It does not, by default, behave like the `Alt` key on other systems, so hotkeys in `json-view-app` won't work out of the box.

### The Solution: Enable "Option as Meta"

To fix this, you must configure your terminal to treat the `Option` key as a "Meta" key, which sends the correct signals for `Alt`-based shortcuts.

#### Visual Studio Code (Integrated Terminal)

1.  Open VS Code Settings (`⌘,`).
2.  Search for `macOptionIsMeta`.
3.  Enable the setting **Terminal > Integrated: Mac Option Is Meta**.

#### macOS Terminal.app

1.  Open Terminal and go to `Terminal` > `Settings...`.
2.  Select the `Profiles` tab and choose your default profile.
3.  Go to the `Keyboard` tab.
4.  Check the box for **"Use Option as Meta key"**.

### The Trade-Off and How to Type Special Characters

Enabling this setting creates a trade-off: you gain `Alt` key functionality in the terminal, but you lose the ability to type special characters directly with the `Option` key.

If you still need to insert a special character, you can use the macOS Character Viewer:

1.  Press `Control + Command + Space`.
2.  Search for the character you need (e.g., "pi").
3.  Double-click it to insert it at your cursor's location.



## Features

* Expand and collapse nodes with the arrow keys or the mouse.
* Search keys or values and jump between matches.
* Open multiple files or read JSON from standard input.
* Mouse interactions: click to select, click left of a label or double-click to expand/collapse, click footer hints, click help dialog to close.
* `--parse-only` mode for pretty-printing JSON without the interactive viewer.
* `--validate` mode for non-interactive JSON validation.
* Optional ASCII-only mode for environments with limited Unicode support.
* Multiple color schemes including colorblind-friendly and monochrome modes; cycle with `t`.
* Configuration via environment variables like `JSON_VIEW_NO_MOUSE`, `JSON_VIEW_ASCII`, and `JSON_VIEW_COLOR_SCHEME`.

## Requirements

* A C++20 compliant compiler
* [CMake](https://cmake.org) 3.20 or newer
* The wide-character `ncursesw` library
* [nlohmann/json](https://github.com/nlohmann/json) (bundled)
* [Turbo Vision](https://github.com/magiblot/tvision) (fetched automatically for `json-view-app`)

## Building

Use the provided Makefile for a simple build:

```sh
make
```

This runs CMake under the hood and places the binaries in `build/json-view`
and `build/json-view-app`.

Alternatively, invoke CMake directly:

```sh
cmake -S . -B build
cmake --build build
```

## Testing

After building, run the parser against the bundled example JSON file:

```sh
make test
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
* Amazon Linux 2023 `.rpm`
* `.tar.gz` archives for Linux and macOS containing `json-view`, docs, and an `install.sh` helper

Download them from the repository's Releases page.

### Amazon Linux 2023

The standard RPM is built on newer Linux distributions and may fail to install
on Amazon Linux 2023. A package built against the libraries bundled with that
distribution is provided in the releases. To recreate it locally run:

```sh
scripts/build-al2023-rpm.sh
```

This generates a package equivalent to the default release but linked against
the system-provided libraries.

## Usage

```sh
json-view path/to/file1.json path/to/file2.json
# or pretty-print and exit
json-view --parse-only path/to/file.json
# validate only
json-view --validate path/to/file.json
# show version
json-view -V
# or read from standard input
cat file1.json | json-view
cat file1.json | json-view --parse-only
cat file1.json | json-view --validate
# disable mouse support
json-view --no-mouse file1.json
# force ASCII tree characters
json-view --ascii file1.json
# or via environment variable
JSON_VIEW_ASCII=1 json-view file1.json
# launch the Turbo Vision interface and open files via the menu
json-view-app
```

If no file argument is supplied `json-view` will read a single JSON document
from standard input.  With `--parse-only` the input is formatted and printed to
standard output. Without it, the interface uses the arrow keys (and the mouse)
to navigate; `--no-mouse` disables mouse handling entirely. Use `--ascii` or
set `JSON_VIEW_ASCII=1` to force plain ASCII characters when Unicode support is
limited.

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
* `y` – copy selected JSON to clipboard via OSC 52 (terminal support required)
* `?` – show a help screen
* `q` – quit the viewer
* Mouse – click to select, click left of a label or double-click to expand/collapse, click footer hints, click anywhere on the help screen to close it

## Documentation

After installation the command line help is available through the manual and
Texinfo pages:

```sh
man json-view
info json-view
```

## License

`json-view` is released under the terms of the [GPLv3 or later License](LICENSE).
