### ✅ Developer To-Do List

The following tasks remain open.

-----

### 1. Clipboard Fallbacks And Messaging Alignment 📋

  - **What to change:** `supportsClipboard()` advertises `wl-copy`/`xclip`/`xsel`, but `copyToClipboard()` only attempts OSC 52. Messaging can be misleading.
  - **How to change:** Either implement fallbacks invoking those tools safely from the ncurses app, or adjust detection/messages. Provide env flags like `JSON_VIEW_NO_CLIPBOARD=1` to disable attempts.

### 2. Lazy Tree Building For Large Files ⚡

  - **What to change:** The full tree is built eagerly, which is slow and memory-heavy for huge JSON files.
  - **How to change:** Build children on demand when expanding a node; consider limiting initial depth and virtualizing rendering to only visible nodes.

### 3. JSON Pointer Path Utilities 🔗

  - **What to change:** Status bar shows a path-like string, but copying/exporting paths is not supported.
  - **How to change:** Add a key to copy the JSON Pointer of the selected node; optionally show pointer in the status bar with a toggle.

### 4. Horizontal Scrolling / Wrapping 📜

  - **What to change:** Long lines truncate; there is no horizontal scroll or wrap toggle.
  - **How to change:** Add horizontal scrolling or a soft-wrap mode; ensure widths respect wide characters and combining marks.

### 5. Continuous Integration For PRs 🧪

  - **What to change:** Release workflow exists, but no CI on pushes/PRs.
  - **How to change:** Add a build + ctest workflow on push/pull_request for Ubuntu and macOS; install ncurses via Homebrew on macOS runner.

### 6. Theming and Accessibility 🎨

  - **What to change:** Single color scheme may be hard to read for some users.
  - **How to change:** Add a monochrome mode and an alternative colorblind-friendly theme; allow toggling at runtime or via flags.
