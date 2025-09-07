### ✅ Developer To-Do List

The following tasks remain open.

-----

### 1. Stabilize JSON Storage To Avoid Dangling Pointers 🧩

  - **What to change:** `Node::value` holds pointers into `jsonDocs` elements stored in a `std::vector`. Pushing additional documents may reallocate the vector and invalidate earlier pointers.
  - **How to change:** Use a container with stable addresses for documents (e.g., `std::deque<json>` or `std::vector<std::unique_ptr<json>>`). Store pointers into those stable allocations before building the trees.

-----

### 2. Clipboard Fallbacks And Messaging Alignment 📋

  - **What to change:** `supportsClipboard()` advertises `wl-copy`/`xclip`/`xsel`, but `copyToClipboard()` only attempts OSC 52. Messaging can be misleading.
  - **How to change:** Either implement fallbacks invoking those tools safely from the ncurses app, or adjust detection/messages. Provide env flags like `JSON_VIEW_NO_CLIPBOARD=1` to disable attempts.

-----

### 3. Versioning From Git Tags / CMake Configure ⚙️

  - **What to change:** `--version` prints a hardcoded "1.0" which can drift from release tags.
  - **How to change:** Inject version at build time using CMake `project(VERSION ...)` + `configure_file` or an env var exported in CI (tag name). Wire it to the `--version` output and README/man pages if needed.

-----

### 4. Improve CMake Portability (ncurses) 🧱

  - **What to change:** The CMakeLists sets Homebrew ncurses paths unconditionally, which may break on non-macOS systems.
  - **How to change:** Prefer `find_package(Curses REQUIRED)` and only set Homebrew paths conditionally on macOS when needed. Optionally add a macOS CI step `brew install ncurses`.

-----

### 5. Unicode/ASCII Rendering Toggle 🌐

  - **What to change:** The UI uses Unicode glyphs and width calculations; some terminals/locales may render poorly.
  - **How to change:** Add an option (e.g., `--ascii` or env var) to disable Unicode icons and use plain ASCII; consider auto-detecting via locale and provide a status hint.

-----

### 6. Tests For Special Number Parsing ✅🧪

  - **What to change:** No automated tests cover `NaN`/`±Infinity` parsing or basic load failures.
  - **How to change:** Add a non-interactive `--validate FILE` mode that parses and exits with status; wire simple ctest cases using files under `examples/`.

-----

### 7. Avoid Reliance On nlohmann::detail Internals 🔧

  - **What to change:** The SAX DOM builder uses internal `nlohmann::detail` types, which are not public API and may change.
  - **How to change:** Replace with a supported approach (custom `json_sax<json>` that builds a DOM or a library-endorsed path) to reduce breakage risk on library updates.
