#include <ncursesw/curses.h>
#include <locale.h>
#include <signal.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <deque>
#include <cmath>
#include <sstream>
#include <chrono>
#include <string>
#include <vector>

#include "json_view_core.hpp"

// Color scheme handling
namespace ColorScheme
{
    enum SchemeId
    {
        DEFAULT,
        COLORBLIND,
        MONOCHROME,
        COUNT
    };

    struct Scheme
    {
        int NORMAL_FG;
        int SELECTION_FG;
        int SEARCH_MATCH_FG;
        int SELECTION_MATCH_FG;
        int TREE_STRUCTURE_FG;
        int EXPAND_INDICATORS_FG;
        int STRING_VALUES_FG;
        int NUMBER_VALUES_FG;
        int BOOLEAN_VALUES_FG;
        int NULL_VALUES_FG;
        int KEY_NAMES_FG;
        int DEFAULT_BG;
        int SELECTION_BG;
        int SELECTION_MATCH_BG;
        const char *name;
        const char *description;
    };

    static const Scheme schemes[COUNT] = {
        {COLOR_WHITE, COLOR_BLACK, COLOR_YELLOW, COLOR_BLACK, COLOR_BLUE, COLOR_MAGENTA,
         COLOR_GREEN, COLOR_GREEN, COLOR_YELLOW, COLOR_RED, COLOR_CYAN, -1, COLOR_CYAN,
         COLOR_GREEN, "default", "Balanced palette with distinct types"},
        {COLOR_WHITE, COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_WHITE, COLOR_WHITE,
         COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_RED, COLOR_WHITE, -1, COLOR_YELLOW,
         COLOR_GREEN, "colorblind", "High-contrast, colorblind-friendly palette"},
        {COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE,
         COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, -1, -1, -1,
         "none", "Colors disabled; using terminal defaults"}};

    enum ColorPairs
    {
        NORMAL_TEXT = 1,
        SELECTION_BG_PAIR,
        SEARCH_MATCH,
        SELECTION_MATCH_BG_PAIR,
        TREE_STRUCTURE,
        EXPAND_INDICATORS,
        STRING_VALUES,
        NUMBER_VALUES,
        BOOLEAN_VALUES,
        NULL_VALUES,
        KEY_NAMES
    };

    inline const int STATUS_BAR = SELECTION_BG_PAIR;
}

// Simple console JSON viewer using nlohmann::json and ncurses.
//
// This program allows you to explore one or more JSON documents from a
// terminal.  Pass file names on the command line to open them, or pipe
// JSON into the program with no arguments.  Each document is presented
// below the other as a collapsible tree.  Navigation uses the arrow
// keys: up/down move between items, right expands the selected branch
// and left collapses it or jumps to the parent.  Use '+' to expand
// every branch and '-' to collapse everything.  Press 's' to search
// keys and 'S' to search values.  When a search is active you can
// press 'n' or 'N' to cycle through the matches and 'c' to clear the
// search.  Press 'h' to view a list of key bindings.  Press 'q' to
// quit.

using json = nlohmann::json;

struct ClickHint
{
    char key;
    int start;
    int end;
};

static std::vector<ClickHint> clickableHints;

// When true, render tree and icons using plain ASCII instead of Unicode
static bool asciiMode = false;
static ColorScheme::SchemeId currentScheme = ColorScheme::DEFAULT;

static ColorScheme::SchemeId parseScheme(const std::string &name)
{
    std::string n;
    n.resize(name.size());
    std::transform(name.begin(), name.end(), n.begin(), ::tolower);
    if (n == "colorblind")
        return ColorScheme::COLORBLIND;
    if (n == "none" || n == "mono" || n == "monochrome")
        return ColorScheme::MONOCHROME;
    return ColorScheme::DEFAULT;
}

static void applyColorScheme(ColorScheme::SchemeId scheme, bool &colours)
{
    currentScheme = scheme;
    if (scheme == ColorScheme::MONOCHROME || !has_colors())
    {
        colours = false;
        attrset(A_NORMAL);
        return;
    }
    colours = true;
    start_color();
    use_default_colors();
    const auto &cs = ColorScheme::schemes[scheme];
    init_pair(ColorScheme::NORMAL_TEXT, cs.NORMAL_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::SELECTION_BG_PAIR, cs.SELECTION_FG, cs.SELECTION_BG);
    init_pair(ColorScheme::SEARCH_MATCH, cs.SEARCH_MATCH_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::SELECTION_MATCH_BG_PAIR, cs.SELECTION_MATCH_FG, cs.SELECTION_MATCH_BG);
    init_pair(ColorScheme::TREE_STRUCTURE, cs.TREE_STRUCTURE_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::EXPAND_INDICATORS, cs.EXPAND_INDICATORS_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::STRING_VALUES, cs.STRING_VALUES_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::NUMBER_VALUES, cs.NUMBER_VALUES_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::BOOLEAN_VALUES, cs.BOOLEAN_VALUES_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::NULL_VALUES, cs.NULL_VALUES_FG, cs.DEFAULT_BG);
    init_pair(ColorScheme::KEY_NAMES, cs.KEY_NAMES_FG, cs.DEFAULT_BG);
}

// Build a short status message describing the current color scheme
static std::string getColorSchemeStatusMessage(bool colours)
{
    if (!colours || currentScheme == ColorScheme::MONOCHROME)
    {
        return std::string("Color scheme: none — Colors disabled");
    }
    const auto &cs = ColorScheme::schemes[currentScheme];
    std::string msg = "Color scheme: ";
    msg += cs.name ? cs.name : "unknown";
    if (cs.description && std::strlen(cs.description) > 0)
    {
        msg += " — ";
        msg += cs.description;
    }
    return msg;
}

// Transient status message state and helpers
static std::string transientStatusMessage;
static std::chrono::steady_clock::time_point transientStatusExpiresAt;
static inline bool hasTransientStatus()
{
    return !transientStatusMessage.empty() && std::chrono::steady_clock::now() < transientStatusExpiresAt;
}
static inline void showTransientStatus(const std::string &msg, int duration_ms)
{
    transientStatusMessage = msg;
    transientStatusExpiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
}

// Display a help screen listing all key bindings.  The overlay
// temporarily clears the screen and waits for any key press before
// returning.
static void showHelp()
{
    clear();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    bool clipboardSupported = osc52Likely();

    std::string copyLine = "  y                Copy selected JSON to clipboard";
    if (!clipboardSupported)
    {
        const char *tmux = std::getenv("TMUX");
        if (tmux && osc52Likely())
        {
            copyLine += " (tmux: enable OSC 52 forwarding)";
        }
        else if (tmux)
        {
            copyLine += " (tmux: requires OSC 52 config)";
        }
        else
        {
            copyLine += " (no terminal support)";
        }
    }

    const std::vector<std::string> lines = {
        "JSON Viewer Key Bindings:",
        "",
        "  ↑/↓              Move selection up or down",
        "  PgUp/PgDn        Move one page up or down",
        "  Home/End         Jump to first or last item",
        "  ←                Collapse the current item or go to its parent",
        "  →                Expand the current item",
        "  +                Expand all items",
        "  -                Collapse all items",
        "  0-9              Expand to nesting level (0=collapse all, 1=first level, etc.)",
        "  s                Search keys",
        "  S                Search values",
        "  n / N            Next / previous search match",
        "  c                Clear search results",
        "  t                Cycle color scheme",
        copyLine,
        "  ?                Show this help screen",
        "  q                Quit the program",
        "",
        "Press any key to return..."};

    int total = lines.size();

    // Find the maximum line width to size the box
    int maxWidth = 0;
    for (const auto &line : lines)
    {
        int lineWidth = getDisplayWidth(line);
        if (lineWidth > maxWidth)
        {
            maxWidth = lineWidth;
        }
    }

    // Add padding for the box
    int boxWidth = maxWidth + 4; // 2 chars padding on each side
    int boxHeight = total + 2;   // top and bottom border

    // Center the entire box
    int startRow = (rows - boxHeight) / 2;
    int startCol = (cols - boxWidth) / 2;

    // Draw top border
    mvaddstr(startRow, startCol, asciiMode ? "+" : "┌");
    for (int i = 1; i <= boxWidth - 1; ++i)
    {
        addstr(asciiMode ? "-" : "─");
    }
    addstr(asciiMode ? "+" : "┐");

    // Draw content with side borders
    for (int i = 0; i < total; ++i)
    {
        mvaddstr(startRow + 1 + i, startCol, asciiMode ? "|" : "│");
        mvaddstr(startRow + 1 + i, startCol + 1, "  "); // Left padding

        // Handle the copy line with special coloring if not supported
        // Find the line by content rather than relying on a fixed index
        if (!clipboardSupported && lines[i].find("Copy selected JSON") != std::string::npos)
        {
            // Dim only the explanatory suffix (starting with " (") if present
            const std::string &full = lines[i];
            size_t suffixPos = full.find(" (");
            if (suffixPos != std::string::npos)
            {
                std::string mainPart = full.substr(0, suffixPos);
                std::string suffix = full.substr(suffixPos);
                addstr(mainPart.c_str());
                attron(A_DIM); // Gray out the unsupported/explanatory part
                addstr(suffix.c_str());
                attroff(A_DIM);
            }
            else
            {
                addstr(full.c_str());
            }
        }
        else
        {
            addstr(lines[i].c_str());
        }

        // Right padding and border
        int lineLen = getDisplayWidth(lines[i]);
        for (int j = lineLen + 2; j < boxWidth - 1; ++j)
        {
            addstr(" ");
        }
        addstr(asciiMode ? "|" : "│");
    }

    // Draw bottom border
    mvaddstr(startRow + boxHeight - 1, startCol, asciiMode ? "+" : "└");
    for (int i = 1; i < boxWidth; ++i)
    {
        addstr(asciiMode ? "-" : "─");
    }
    addstr(asciiMode ? "+" : "┘");

    refresh();
    int ch = getch();
    if (ch == KEY_MOUSE)
    {
        MEVENT ev;
        getmouse(&ev); // discard, clicking anywhere closes
    }
}

// Prompt the user for a search term.  The prompt appears on the
// bottom line and the typed characters are echoed.  Return the
// entered string with leading/trailing whitespace trimmed.
static std::string promptSearch(const char *prompt)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    // Draw prompt and echo input
    echo();
    curs_set(1);
    mvprintw(rows - 1, 0, "%s", prompt);
    clrtoeol();
    char buf[512];
    getnstr(buf, sizeof(buf) - 1);
    noecho();
    curs_set(0);
    // Trim whitespace
    std::string s(buf);
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos)
        return std::string();
    return s.substr(start, end - start + 1);
}

// Display command-line usage information
static void showUsage(const char *progName)
{
    std::cout << "json-view - Interactive JSON viewer with tree navigation\n\n";
    std::cout << "USAGE:\n";
    std::cout << "  " << progName << " [--parse-only|--validate] [--no-mouse] [--ascii] [--color-scheme NAME] [file1.json] [file2.json] ...\n";
    std::cout << "  cat data.json | " << progName << " [--parse-only|--validate] [--no-mouse] [--ascii] [--color-scheme NAME]\n\n";
    std::cout << "DESCRIPTION:\n";
    std::cout << "  A simple console JSON viewer using ncurses for interactive tree navigation.\n";
    std::cout << "  Pass JSON file names as arguments to open them, or pipe JSON into the program\n";
    std::cout << "  with no arguments. Each document is presented as a collapsible tree.\n\n";
    std::cout << "NAVIGATION:\n";
    std::cout << (asciiMode ? "  Up/Down  " : "  ↑/↓       ") << "Move selection up or down\n";
    std::cout << "  PgUp/PgDn Move one page up or down\n";
    std::cout << "  Home/End  Jump to first or last item\n";
    std::cout << (asciiMode ? "  <-       " : "  ←         ") << "Collapse item or go to parent\n";
    std::cout << (asciiMode ? "  ->       " : "  →         ") << "Expand the current item\n";
    std::cout << "  +         Expand all items\n";
    std::cout << "  -         Collapse all items\n";
    std::cout << "  0-9       Expand to nesting level (0=collapse all)\n";
    std::cout << "  s         Search keys\n";
    std::cout << "  S         Search values\n";
    std::cout << "  n/N       Next/previous search match\n";
    std::cout << "  c         Clear search results\n";
    std::cout << "  t         Cycle color scheme\n";
    std::cout << "  y         Copy selected JSON to clipboard\n";
    std::cout << "  ?         Show help screen\n";
    std::cout << "  q         Quit the program\n";
    std::cout << "  Mouse     Click to select, click left of label or double-click to expand/collapse, click footer hints, click help screen to close\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "  -V, --version     Show version information\n";
    std::cout << "  -p, --parse-only  Parse input and pretty-print JSON then exit\n";
    std::cout << "      --validate    Validate JSON input and exit with status\n";
    std::cout << "      --no-mouse    Disable mouse support (or set JSON_VIEW_NO_MOUSE=1)\n";
    std::cout << "      --ascii       Use ASCII tree/indicator characters (or set JSON_VIEW_ASCII=1)\n";
    std::cout << "      --color-scheme NAME  Select color scheme (default, colorblind, none)\n"
              << "                     (or set JSON_VIEW_COLOR_SCHEME)\n\n";
    std::cout << "EXAMPLES:\n";
    std::cout << "  " << progName << " config.json data.json\n";
    std::cout << "  " << progName << " --parse-only config.json\n";
    std::cout << "  echo '{\"key\":\"value\"}' | " << progName << " --parse-only\n";
    std::cout << "  curl -s https://api.example.com/data | " << progName << "\n\n";
    std::cout << "AUTHOR:\n";
    std::cout << "  Dr. C. Klukas\n\n";
    std::cout << "LICENSE:\n";
    std::cout << "  GPLv3 or later\n";
}

// Helper function to draw a single line in the display
static void drawLine(int row, int idx, const Node *node, size_t selected,
                     const SearchState &search, int cols, bool colours)
{
    std::string prefix = buildPrefix(node);

    // Calculate available width for label (accounting for prefix, indicator, and icon)
    int prefixWidth = getDisplayWidth(prefix) + 4; // +4 for indicator and icon space
    int availableWidth = cols - prefixWidth - 5;   // -5 for some margin
    std::string typeIcon = getTypeIcon(node);
    std::string contentLabel = getContentLabelWithSearch(node, search, availableWidth);
    int contentWidthForHighlight = getDisplayWidth(contentLabel);

    // Add expand/collapse indicator with subtle Unicode triangles (ASCII when enabled)
    std::string indicator;

    if (!node->children.empty())
    {
        // For expandable nodes (objects and arrays), use regular expand/collapse indicator
        indicator = node->expanded ? (asciiMode ? "v " : "▼ ") : (asciiMode ? "> " : "▶ ");
    }
    else if (!typeIcon.empty())
    {
        // For leaf nodes with type icons, use the icon as the indicator
        indicator = "";
    }
    else
    {
        indicator = "  ";
    }

    // Determine selection and match status
    bool isSelected = (idx == (int)selected);
    bool isMatch = false;
    if (!search.term.empty())
    {
        // Determine if current node is in search matches
        for (const Node *m : search.matches)
        {
            if (m == node)
            {
                isMatch = true;
                break;
            }
        }
    }

    // Clear the line first
    mvhline(row, 0, ' ', cols);

    // Render line with colors
    mvaddnstr(row, 0, "", 0); // Position cursor
    int pos = 0;

    // Render tree structure (prefix) in blue
    if (!prefix.empty())
    {
        if (colours)
            attron(COLOR_PAIR(ColorScheme::TREE_STRUCTURE));
        addnstr(prefix.c_str(), prefix.length());
        if (colours)
            attroff(COLOR_PAIR(ColorScheme::TREE_STRUCTURE));
        pos += getDisplayWidth(prefix);
    }

    // Render expand/collapse indicator in magenta
    if (!indicator.empty())
    {
        if (colours)
            attron(COLOR_PAIR(ColorScheme::EXPAND_INDICATORS));
        addnstr(indicator.c_str(), indicator.length());
        if (colours)
            attroff(COLOR_PAIR(ColorScheme::EXPAND_INDICATORS));
        pos += getDisplayWidth(indicator);
    }

    // Render type icon right after indicator (for leaf nodes, this replaces the spaces)
    if (!typeIcon.empty() && node->children.empty())
    {
        if (colours)
            attron(COLOR_PAIR(ColorScheme::EXPAND_INDICATORS)); // Use same color as indicators
        addnstr(typeIcon.c_str(), typeIcon.length());
        if (colours)
            attroff(COLOR_PAIR(ColorScheme::EXPAND_INDICATORS));
        pos += getDisplayWidth(typeIcon);
    }

    // Render content with proper colors
    if (colours)
    {
        const json *v = node->value;

        // Special handling for arrays with preview
        if (v->is_array() && !node->expanded && !node->isDummyRoot)
        {
            // Build base label (key + count info)
            size_t count = v->size();
            std::string baseLabel = node->key + " (list, " + std::to_string(count) + (count == 1 ? " item)" : " items)");

            // If array is empty, just render the base label
            if (v->empty())
            {
                addnstr(baseLabel.c_str(), baseLabel.length());
                contentWidthForHighlight = getDisplayWidth(baseLabel);
            }
            else
            {
                // Render base label
                addnstr(baseLabel.c_str(), baseLabel.length());
                int baseWidth = getDisplayWidth(baseLabel);

                // Render preview directly from JSON with colors
                addstr(": ");

                int previewPrintedWidth = 2; // account for ": "
                int previewBudget = std::max(0, availableWidth - baseWidth); // budget for preview part

                bool first = true;
                for (const auto &item : *v)
                {
                    std::string token;
                    int colorPair = ColorScheme::NORMAL_TEXT;

                    if (item.is_string())
                    {
                        token = "\"" + item.get<std::string>() + "\"";
                        colorPair = ColorScheme::STRING_VALUES;
                    }
                    else if (item.is_number())
                    {
                        token = item.dump();
                        colorPair = ColorScheme::NUMBER_VALUES;
                    }
                    else if (item.is_boolean())
                    {
                        token = item.get<bool>() ? "true" : "false";
                        colorPair = ColorScheme::BOOLEAN_VALUES;
                    }
                    else if (item.is_null())
                    {
                        token = "null";
                        colorPair = ColorScheme::NULL_VALUES;
                    }
                    else
                    {
                        token = item.is_object() ? "{...}" : "[...]";
                        colorPair = ColorScheme::NORMAL_TEXT;
                    }

                    int sepWidth = first ? 0 : 2; // ", "
                    int tokenWidth = getDisplayWidth(token);

                    // Reserve space for trailing ellipsis if we can't fit all items
                    if (previewPrintedWidth + sepWidth + tokenWidth > previewBudget - 3)
                    {
                        // Not enough space; add ellipsis if we have printed something
                        if (previewPrintedWidth < previewBudget)
                        {
                            addnstr("...", 3);
                            previewPrintedWidth += 3;
                        }
                        break;
                    }

                    if (!first)
                    {
                        addstr(", ");
                        previewPrintedWidth += 2;
                    }

                    if (colorPair != ColorScheme::NORMAL_TEXT)
                    {
                        attron(COLOR_PAIR(colorPair));
                        addnstr(token.c_str(), token.length());
                        attroff(COLOR_PAIR(colorPair));
                    }
                    else
                    {
                        addnstr(token.c_str(), token.length());
                    }

                    previewPrintedWidth += tokenWidth;
                    first = false;
                }

                contentWidthForHighlight = baseWidth + previewPrintedWidth;
            }
        }
        else if (!v->is_object() && !v->is_array() && !node->isDummyRoot)
        {
            // For primitive values, separate key and value
            size_t colonPos = contentLabel.find(": ");
            if (colonPos != std::string::npos)
            {
                // Render key in white
                std::string keyPart = contentLabel.substr(0, colonPos);
                attron(COLOR_PAIR(ColorScheme::KEY_NAMES));
                addnstr(keyPart.c_str(), keyPart.length());
                attroff(COLOR_PAIR(ColorScheme::KEY_NAMES));

                // Render colon
                addstr(": ");

                // Render value in appropriate color
                std::string valuePart = contentLabel.substr(colonPos + 2);
                int valueColorPair = ColorScheme::NORMAL_TEXT; // default
                if (v->is_string())
                    valueColorPair = ColorScheme::STRING_VALUES;
                else if (v->is_number())
                    valueColorPair = ColorScheme::NUMBER_VALUES;
                else if (v->is_boolean())
                    valueColorPair = ColorScheme::BOOLEAN_VALUES;
                else if (v->is_null())
                    valueColorPair = ColorScheme::NULL_VALUES;

                attron(COLOR_PAIR(valueColorPair));
                addnstr(valuePart.c_str(), valuePart.length());
                attroff(COLOR_PAIR(valueColorPair));
            }
            else
            {
                // No colon found, just render normally
                addnstr(contentLabel.c_str(), contentLabel.length());
            }
        }
        else
        {
            // For objects/expanded arrays, render normally
            addnstr(contentLabel.c_str(), contentLabel.length());
        }
    }
    else
    {
        // No colors, just add the content
        addnstr(contentLabel.c_str(), contentLabel.length());
    }

    // Calculate actual rendered text length for proper highlighting
    int actualTextLen = pos + contentWidthForHighlight;

    // Apply selection/match highlighting with minimal extra space
    if (isSelected || isMatch)
    {
        int highlightLen = actualTextLen;
        int attr = A_NORMAL;
        int colorPair = ColorScheme::NORMAL_TEXT;
        if (isSelected && isMatch)
        {
            attr = A_REVERSE | A_BOLD;
            colorPair = ColorScheme::SELECTION_MATCH_BG_PAIR;
        }
        else if (isSelected)
        {
            attr = A_REVERSE;
            colorPair = ColorScheme::SELECTION_BG_PAIR;
        }
        else if (isMatch)
        {
            attr = A_BOLD;
            colorPair = ColorScheme::SEARCH_MATCH;
        }

        if (colours)
        {
            mvchgat(row, 0, highlightLen, attr | COLOR_PAIR(colorPair), 0, NULL);
        }
        else
        {
            mvchgat(row, 0, highlightLen, attr, 0, NULL);
        }
    }
}

// Helper function to draw the status bar
static void drawStatusBar(int statusRow, size_t selected, const std::vector<const Node *> &visible,
                          const SearchState &search, int cols, bool colours)
{
    std::string status;
    clickableHints.clear();

    // If a transient message is active, show it and return
    if (hasTransientStatus())
    {
        status = transientStatusMessage;
        if (getDisplayWidth(status) > cols)
        {
            status = status.substr(0, cols);
        }
        mvhline(statusRow, 0, ' ', cols);
        mvaddnstr(statusRow, 0, status.c_str(), cols);
        mvchgat(statusRow, 0, getDisplayWidth(status), colours ? COLOR_PAIR(ColorScheme::STATUS_BAR) : A_REVERSE, 0, NULL);
        return;
    }

    // Show path of selected node
    {
        const Node *cur = visible[selected];
        std::vector<std::string> parts;
        while (cur != nullptr)
        {
            parts.push_back(cur->key);
            cur = cur->parent;
        }
        std::string path;
        for (int i = (int)parts.size() - 1; i >= 0; --i)
        {
            if (i == (int)parts.size() - 1)
            {
                // First part (root) - extract just filename if it's a path
                std::string rootKey = parts[i];
                size_t lastSlash = rootKey.find_last_of('/');
                if (lastSlash != std::string::npos)
                {
                    rootKey = rootKey.substr(lastSlash + 1); // Just the filename
                }
                path += rootKey;
            }
            else
            {
                // Subsequent parts - add separator
                path += "/" + parts[i];
            }
        }
        status = path.empty() ? "/" : path;
    }

    int curWidth = getDisplayWidth(status);

    auto addHint = [&](char key, const std::string &label, bool addComma) {
        if (addComma)
        {
            status += ", ";
            curWidth += 2;
        }
        int start = curWidth;
        std::string token = std::string(1, key) + ":" + label;
        status += token;
        curWidth += token.size();
        clickableHints.push_back({key, start, curWidth});
    };

    if (!search.term.empty())
    {
        int total = search.matches.size();
        int curIdx = (total == 0 ? 0 : search.currentIndex + 1);
        status += "   [search '" + search.term + "' " + std::to_string(curIdx) + "/" + std::to_string(total) + "]";
        curWidth = getDisplayWidth(status);
        status += "   (";
        curWidth += 4;
        addHint('n', "next", false);
        addHint('N', "prev", true);
        addHint('c', "clear", true);
        status += ")";
    }
    else
    {
        status += "   (";
        curWidth += 4;
        addHint('?', "help", false);
        addHint('q', "quit", true);
        status += ")";
    }

    if (getDisplayWidth(status) > cols)
    {
        status = status.substr(0, cols);
    }

    // Clear the status line first
    mvhline(statusRow, 0, ' ', cols);
    mvaddnstr(statusRow, 0, status.c_str(), cols);
    mvchgat(statusRow, 0, getDisplayWidth(status), colours ? COLOR_PAIR(ColorScheme::STATUS_BAR) : A_REVERSE, 0, NULL);
}

// Helper function to redraw from a specific row downwards (for expand/collapse optimization)
static void drawFromRowDownwards(int startRow, int scrollOffset, const std::vector<const Node *> &visible,
                                 size_t selected, const SearchState &search, int rows, int cols, bool colours)
{
    int displayRows = rows - 1; // Reserve last row for status bar

    // Clear from startRow to bottom of display area
    for (int i = startRow; i < displayRows; ++i)
    {
        mvhline(i, 0, ' ', cols);
    }

    // Redraw from startRow downwards
    for (int i = startRow; i < displayRows; ++i)
    {
        int idx = scrollOffset + i;
        if (idx >= (int)visible.size())
            break;
        drawLine(i, idx, visible[idx], selected, search, cols, colours);
    }

    // Always update status bar
    drawStatusBar(rows - 1, selected, visible, search, cols, colours);
}

// Entry point
int main(int argc, char **argv)
{
    // Enable UTF-8 locale for proper Unicode support
    setlocale(LC_ALL, "");

    bool parseOnly = false;
    bool validateOnly = false;
    bool enableMouse = true;
    const char *envAscii = std::getenv("JSON_VIEW_ASCII");
    if (envAscii && *envAscii)
    {
        asciiMode = true;
    }
    const char *envNoMouse = std::getenv("JSON_VIEW_NO_MOUSE");
    if (envNoMouse && *envNoMouse)
    {
        enableMouse = false;
    }
    const char *envScheme = std::getenv("JSON_VIEW_COLOR_SCHEME");
    if (envScheme && *envScheme)
    {
        currentScheme = parseScheme(envScheme);
    }
    std::vector<const char *> files;
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            showUsage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0)
        {
            std::cout << "json-view " << JSON_VIEW_VERSION << "\n";
            std::cout << "Built on " << __DATE__ << " " << __TIME__ << "\n";
            std::cout << "Author: (c) 2025, Dr. C. Klukas\n";
            return 0;
        }
        if (strcmp(arg, "--parse-only") == 0 || strcmp(arg, "-p") == 0)
        {
            parseOnly = true;
            continue;
        }
        if (strcmp(arg, "--validate") == 0)
        {
            validateOnly = true;
            continue;
        }
        if (strcmp(arg, "--no-mouse") == 0)
        {
            enableMouse = false;
            continue;
        }
        if (strcmp(arg, "--ascii") == 0)
        {
            asciiMode = true;
            continue;
        }
        if (strcmp(arg, "--color-scheme") == 0 && i + 1 < argc)
        {
            currentScheme = parseScheme(argv[++i]);
            continue;
        }
        const char *prefix = "--color-scheme=";
        if (strncmp(arg, prefix, strlen(prefix)) == 0)
        {
            currentScheme = parseScheme(arg + strlen(prefix));
            continue;
        }
        files.push_back(arg);
    }

    // Parse JSON files or standard input
    std::vector<std::unique_ptr<Node>> roots;
    std::deque<json> jsonDocs; // deque guarantees stable addresses for elements
    bool anyParsed = false;
    bool allParsed = true;
    for (const char *filename : files)
    {
        std::ifstream f(filename);
        if (!f)
        {
            std::cerr << "Failed to open file: " << filename << std::endl;
            continue;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string contents = ss.str();

        // Store the actual file size
        size_t fileSize = contents.size();
        fileSizes[filename] = fileSize;

        try
        {
            json doc = parseJsonWithSpecialNumbers(contents);
            anyParsed = true;
            if (parseOnly)
            {
                printFormattedJson(doc);
                std::cout << "\n";
            }
            else if (!validateOnly)
            {
                jsonDocs.push_back(std::move(doc));
                const json *ptr = &jsonDocs.back();
                auto root = buildTree(ptr, filename, nullptr, true);
                roots.push_back(std::move(root));
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << "Error parsing JSON in " << filename << ": " << ex.what() << std::endl;
            allParsed = false;
        }
    }

    if (files.empty())
    {
        // Read from stdin and parse as a single JSON document
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        std::string contents = ss.str();
        if (!contents.empty())
        {
            size_t contentSize = contents.size();
            fileSizes["(stdin)"] = contentSize;

            try
            {
                json doc = parseJsonWithSpecialNumbers(contents);
                anyParsed = true;
                if (parseOnly)
                {
                    printFormattedJson(doc);
                    std::cout << "\n";
                }
                else if (!validateOnly)
                {
                    jsonDocs.push_back(std::move(doc));
                    const json *ptr = &jsonDocs.back();
                    auto root = buildTree(ptr, "(stdin)", nullptr, true);
                    roots.push_back(std::move(root));
                }
            }
            catch (const std::exception &ex)
            {
                std::cerr << "Error parsing JSON from stdin: " << ex.what() << std::endl;
                allParsed = false;
            }
        }
    }

    if (validateOnly)
    {
        if (!anyParsed || !allParsed)
            return 1;
        return 0;
    }

    if (parseOnly)
    {
        if (!anyParsed)
        {
            std::cerr << "No valid JSON documents provided." << std::endl;
            return 1;
        }
        return 0;
    }

    // Mark last child among roots so that prefixes are drawn properly
    for (size_t i = 0; i < roots.size(); ++i)
    {
        roots[i]->isLastChild = (i == roots.size() - 1);
    }
    // If nothing was parsed there is nothing to display
    if (roots.empty())
    {
        std::cerr << "No valid JSON documents provided." << std::endl;
        return 1;
    }
    // Initialise curses
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (enableMouse)
    {
        mousemask(ALL_MOUSE_EVENTS, NULL);
    }

    bool colours = false;
    applyColorScheme(currentScheme, colours);

    // Enable window resize detection
    struct sigaction sa;
    sa.sa_handler = [](int)
    {
        endwin();
        refresh();
        clear();
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);

    // State variables
    SearchState search;
    std::vector<const Node *> visible;
    // Start with first visible node selected
    size_t selected = 0;
    size_t previousSelected = SIZE_MAX; // Track previous selection for efficient updates
    int scrollOffset = 0;
    int previousScrollOffset = -1;  // Track previous scroll offset
    bool needFullRedraw = true;     // Flag to force full redraw when needed
    bool needPartialRedraw = false; // Flag for partial redraw from current line downwards

    // Main loop
    bool running = true;
    while (running)
    {
        // Collect visible nodes
        visible.clear();
        for (auto &r : roots)
        {
            collectVisible(r.get(), visible);
        }
        if (visible.empty())
        {
            // Should never happen
            mvprintw(0, 0, "No data to display");
            refresh();
            getch();
            break;
        }
        // Constrain selected index
        if (selected >= visible.size())
            selected = visible.size() - 1;

        // Get screen dimensions
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int displayRows = rows - 1; // Reserve the last row for status bar

        // Adjust scroll offset to keep selected row on screen
        int newScrollOffset = scrollOffset;
        if ((int)selected < scrollOffset)
            newScrollOffset = selected;
        if ((int)selected >= scrollOffset + displayRows)
            newScrollOffset = selected - displayRows + 1;

        // Detect scroll changes and direction
        bool scrollChanged = (newScrollOffset != previousScrollOffset);
        int scrollDirection = 0; // 0=no scroll, 1=down, -1=up
        int scrollAmount = 0;
        if (scrollChanged && previousScrollOffset != -1)
        {
            scrollAmount = newScrollOffset - previousScrollOffset;
            scrollDirection = (scrollAmount > 0) ? 1 : -1;
        }
        scrollOffset = newScrollOffset;

        // Determine what kind of update we need
        if (needFullRedraw || (scrollChanged && previousScrollOffset == -1))
        {
            // Full redraw needed - first time or major change
            clear();
            for (int i = 0; i < displayRows; ++i)
            {
                int idx = scrollOffset + i;
                if (idx >= (int)visible.size())
                    break;
                drawLine(i, idx, visible[idx], selected, search, cols, colours);
            }
            drawStatusBar(rows - 1, selected, visible, search, cols, colours);
            needFullRedraw = false;
        }
        else if (needPartialRedraw)
        {
            // Partial redraw from current line downwards (for expand/collapse)
            int currentRow = (int)selected - scrollOffset;
            if (currentRow >= 0 && currentRow < displayRows)
            {
                drawFromRowDownwards(currentRow, scrollOffset, visible, selected, search, rows, cols, colours);
            }
            else
            {
                // If selected is not visible, fall back to full redraw
                clear();
                for (int i = 0; i < displayRows; ++i)
                {
                    int idx = scrollOffset + i;
                    if (idx >= (int)visible.size())
                        break;
                    drawLine(i, idx, visible[idx], selected, search, cols, colours);
                }
                drawStatusBar(rows - 1, selected, visible, search, cols, colours);
            }
            needPartialRedraw = false;
        }
        else if (scrollChanged && abs(scrollAmount) < displayRows)
        {
            // Optimized scrolling - scroll existing content and draw new lines
            if (scrollDirection == 1)
            {
                // Scrolling down - content moves up, new lines appear at bottom
                // Set scrolling region to exclude status bar
                scrollok(stdscr, TRUE);
                setscrreg(0, displayRows - 1);

                // Scroll the display region up by scrollAmount lines
                for (int i = 0; i < scrollAmount; ++i)
                {
                    scrl(1);
                }

                // Draw the new lines that appeared at the bottom
                for (int i = 0; i < scrollAmount; ++i)
                {
                    int row = displayRows - scrollAmount + i;
                    int idx = scrollOffset + row;
                    if (idx < (int)visible.size())
                    {
                        drawLine(row, idx, visible[idx], selected, search, cols, colours);
                    }
                    else
                    {
                        // Clear any remaining lines
                        mvhline(row, 0, ' ', cols);
                    }
                }

                // Reset scrolling region
                setscrreg(0, rows - 1);
                scrollok(stdscr, FALSE);
            }
            else
            {
                // Scrolling up - content moves down, new lines appear at top
                // Set scrolling region to exclude status bar
                scrollok(stdscr, TRUE);
                setscrreg(0, displayRows - 1);

                // Scroll the display region down by scrollAmount lines
                for (int i = 0; i < -scrollAmount; ++i)
                {
                    scrl(-1);
                }

                // Draw the new lines that appeared at the top
                for (int i = 0; i < -scrollAmount; ++i)
                {
                    int row = i;
                    int idx = scrollOffset + row;
                    if (idx < (int)visible.size())
                    {
                        drawLine(row, idx, visible[idx], selected, search, cols, colours);
                    }
                }

                // Reset scrolling region
                setscrreg(0, rows - 1);
                scrollok(stdscr, FALSE);
            }

            // Update selection highlighting if needed
            if (previousSelected != SIZE_MAX && previousSelected != selected)
            {
                int prevRow = (int)previousSelected - scrollOffset;
                int currentRow = (int)selected - scrollOffset;

                if (prevRow >= 0 && prevRow < displayRows && previousSelected < visible.size())
                {
                    drawLine(prevRow, previousSelected, visible[previousSelected], selected, search, cols, colours);
                }
                if (currentRow >= 0 && currentRow < displayRows)
                {
                    drawLine(currentRow, selected, visible[selected], selected, search, cols, colours);
                }
            }

            // Always update status bar
            drawStatusBar(rows - 1, selected, visible, search, cols, colours);
        }
        else if (scrollChanged)
        {
            // Large scroll change - fall back to full redraw
            clear();
            for (int i = 0; i < displayRows; ++i)
            {
                int idx = scrollOffset + i;
                if (idx >= (int)visible.size())
                    break;
                drawLine(i, idx, visible[idx], selected, search, cols, colours);
            }
            drawStatusBar(rows - 1, selected, visible, search, cols, colours);
        }
        else
        {
            // Selective update - only update changed lines
            // Debug: Add temporary logging
            // fprintf(stderr, "SELECTIVE UPDATE: prev=%zu curr=%zu\n", previousSelected, selected);
            // Update previous selection line if it's visible and different
            if (previousSelected != SIZE_MAX && previousSelected != selected)
            {
                int prevRow = (int)previousSelected - scrollOffset;
                if (prevRow >= 0 && prevRow < displayRows && previousSelected < visible.size())
                {
                    drawLine(prevRow, previousSelected, visible[previousSelected], selected, search, cols, colours);
                }
            }

            // Update current selection line if it's visible
            int currentRow = (int)selected - scrollOffset;
            if (currentRow >= 0 && currentRow < displayRows)
            {
                drawLine(currentRow, selected, visible[selected], selected, search, cols, colours);
            }

            // Always update status bar (path changes with selection)
            drawStatusBar(rows - 1, selected, visible, search, cols, colours);
        }

        // Remember current state for next iteration
        previousSelected = selected;
        previousScrollOffset = scrollOffset;

        refresh();

        // Use non-blocking input when a transient status is active so it can expire
        if (hasTransientStatus())
        {
            auto now = std::chrono::steady_clock::now();
            auto remaining = transientStatusExpiresAt - now;
            int wait_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            if (wait_ms < 1)
                wait_ms = 1;
            timeout(wait_ms);
        }
        else
        {
            timeout(-1); // blocking
        }

        // Process input
        int ch = getch();
        switch (ch)
        {
        case KEY_MOUSE:
        {
            MEVENT ev;
            if (getmouse(&ev) == OK)
            {
                int rows, cols;
                getmaxyx(stdscr, rows, cols);
                int displayRows = rows - 1;
                if (ev.bstate & BUTTON1_DOUBLE_CLICKED)
                {
                    if (ev.y < displayRows)
                    {
                        size_t idx = scrollOffset + ev.y;
                        if (idx < visible.size())
                        {
                            selected = idx;
                            Node *n = const_cast<Node *>(visible[selected]);
                            if (!n->children.empty())
                            {
                                n->expanded = !n->expanded;
                                needPartialRedraw = true;
                            }
                        }
                    }
                }
                else if (ev.bstate & BUTTON1_CLICKED)
                {
                    if (ev.y < displayRows)
                    {
                        size_t idx = scrollOffset + ev.y;
                        if (idx < visible.size())
                        {
                            selected = idx;
                            Node *n = const_cast<Node *>(visible[selected]);
                            int prefixClick = getDisplayWidth(buildPrefix(n)) + 2;
                            if (ev.x < prefixClick && !n->children.empty())
                            {
                                n->expanded = !n->expanded;
                                needPartialRedraw = true;
                            }
                        }
                    }
                    else if (ev.y == rows - 1)
                    {
                        for (const auto &h : clickableHints)
                        {
                            if (ev.x >= h.start && ev.x < h.end)
                            {
                                ungetch(h.key);
                                break;
                            }
                        }
                    }
                }
            }
        }
        break;
        case KEY_UP:
        case 'k':
            if (selected > 0)
                --selected;
            break;
        case KEY_DOWN:
        case 'j':
            if (selected + 1 < visible.size())
                ++selected;
            break;
        case KEY_NPAGE: // Page Down
        {
            int rows, cols;
            getmaxyx(stdscr, rows, cols);
            int pageSize = rows - 2; // Reserve space for status bar
            selected = std::min(selected + static_cast<size_t>(pageSize), visible.size() - 1);
        }
        break;
        case KEY_PPAGE: // Page Up
        {
            int rows, cols;
            getmaxyx(stdscr, rows, cols);
            int pageSize = rows - 2; // Reserve space for status bar
            selected = (selected >= static_cast<size_t>(pageSize)) ? selected - pageSize : 0;
        }
        break;
        case KEY_HOME: // Home/Pos1
            selected = 0;
            break;
        case KEY_END: // End
            selected = visible.size() - 1;
            break;
        case KEY_LEFT:
        case 'h':
        {
            Node *n = const_cast<Node *>(visible[selected]);
            if (n->expanded && !n->children.empty())
            {
                n->expanded = false;
                needPartialRedraw = true; // Only redraw from current line downwards
            }
            else if (n->parent != nullptr)
            {
                // Move to parent
                const Node *parent = n->parent;
                // Find parent index in visible list
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == parent)
                    {
                        selected = i;
                        break;
                    }
                }
            }
        }
        break;
        case KEY_RIGHT:
        case 'l':
        {
            Node *n = const_cast<Node *>(visible[selected]);
            if (!n->children.empty())
            {
                n->expanded = true;
                needPartialRedraw = true; // Only redraw from current line downwards
            }
        }
        break;
        case '+':
        case '=':
            for (auto &r : roots)
            {
                expandAll(r.get());
            }
            needFullRedraw = true; // Tree structure changed significantly
            break;
        case '-':
        case '_':
        {
            // Remember the currently selected node
            const Node *selectedNode = nullptr;
            if (selected < visible.size())
            {
                selectedNode = visible[selected];
            }

            // Collapse all nodes but preserve path to selected node
            for (auto &r : roots)
            {
                collapseAll(r.get(), true);
            }

            // Ensure the path to the selected node remains expanded
            if (selectedNode != nullptr)
            {
                expandPath(const_cast<Node *>(selectedNode));

                // Rebuild visible list and find new selection index
                visible.clear();
                for (auto &r : roots)
                {
                    collectVisible(r.get(), visible);
                }

                // Find the selected node in the new visible list
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == selectedNode)
                    {
                        selected = i;
                        break;
                    }
                }
            }
            needFullRedraw = true; // Tree structure changed significantly
        }
        break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        {
            // Expand to specific nesting level
            int level = ch - '0';

            // Remember the currently selected node
            const Node *selectedNode = nullptr;
            if (selected < visible.size())
            {
                selectedNode = visible[selected];
            }

            // Apply expansion level to all roots
            for (auto &r : roots)
            {
                expandToLevel(r.get(), level, 0);
            }

            // Ensure the path to the selected node remains visible if possible
            if (selectedNode != nullptr)
            {
                // Check if selected node is still at a visible level
                int nodeLevel = 0;
                const Node *current = selectedNode;
                while (current->parent != nullptr)
                {
                    nodeLevel++;
                    current = current->parent;
                    if (current->isDummyRoot)
                        break;
                }

                // If the selected node's level is within the expansion level, expand its path
                if (level == 0 || nodeLevel <= level)
                {
                    expandPath(const_cast<Node *>(selectedNode));
                }

                // Rebuild visible list and find new selection index
                visible.clear();
                for (auto &r : roots)
                {
                    collectVisible(r.get(), visible);
                }

                // Find the selected node in the new visible list, or keep it at 0 if not found
                selected = 0;
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == selectedNode)
                    {
                        selected = i;
                        break;
                    }
                }
            }
            needFullRedraw = true; // Tree structure changed significantly
        }
        break;
        case 's':
        case '/':
        {
            std::string term = promptSearch("Search key: ");
            // Lowercase the term for case‑insensitive comparison
            std::string lower = term;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            search.term = lower;
            search.searchKeys = true;
            search.searchValues = false;
            search.matches.clear();
            search.currentIndex = 0;
            for (auto &r : roots)
            {
                searchTree(r.get(), lower, true, false, search.matches);
            }
            if (!search.matches.empty())
            {
                // Jump to first match
                Node *match = const_cast<Node *>(search.matches[0]);
                expandPath(match);
                // Update visible list and selected index
                visible.clear();
                for (auto &r : roots)
                    collectVisible(r.get(), visible);
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == match)
                    {
                        selected = i;
                        break;
                    }
                }
            }
            needFullRedraw = true; // Search changed display state
        }
        break;
        case 'S':
        {
            std::string term = promptSearch("Search value: ");
            std::string lower = term;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            search.term = lower;
            search.searchKeys = false;
            search.searchValues = true;
            search.matches.clear();
            search.currentIndex = 0;
            for (auto &r : roots)
            {
                searchTree(r.get(), lower, false, true, search.matches);
            }
            if (!search.matches.empty())
            {
                Node *match = const_cast<Node *>(search.matches[0]);
                expandPath(match);
                visible.clear();
                for (auto &r : roots)
                    collectVisible(r.get(), visible);
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == match)
                    {
                        selected = i;
                        break;
                    }
                }
            }
            needFullRedraw = true; // Search changed display state
        }
        break;
        case 'n':
            if (!search.term.empty() && !search.matches.empty())
            {
                const Node *currentNode = visible[selected];
                auto it = std::find(search.matches.begin(), search.matches.end(), currentNode);
                if (it != search.matches.end())
                {
                    search.currentIndex = std::distance(search.matches.begin(), it);
                }
                search.currentIndex = (search.currentIndex + 1) % search.matches.size();
                Node *nextMatch = const_cast<Node *>(search.matches[search.currentIndex]);
                expandPath(nextMatch);
                visible.clear();
                for (auto &r : roots)
                    collectVisible(r.get(), visible);
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == nextMatch)
                    {
                        selected = i;
                        break;
                    }
                }
                needFullRedraw = true; // Tree expansion may have changed
            }
            break;
        case 'N':
            if (!search.term.empty() && !search.matches.empty())
            {
                const Node *currentNode = visible[selected];
                auto it = std::find(search.matches.begin(), search.matches.end(), currentNode);
                if (it != search.matches.end())
                {
                    search.currentIndex = std::distance(search.matches.begin(), it);
                }
                search.currentIndex = (search.currentIndex - 1 + search.matches.size()) % search.matches.size();
                Node *prevMatch = const_cast<Node *>(search.matches[search.currentIndex]);
                expandPath(prevMatch);
                visible.clear();
                for (auto &r : roots)
                    collectVisible(r.get(), visible);
                for (size_t i = 0; i < visible.size(); ++i)
                {
                    if (visible[i] == prevMatch)
                    {
                        selected = i;
                        break;
                    }
                }
                needFullRedraw = true; // Tree expansion may have changed
            }
            break;
        case 'c':
            // Clear search
            search.term.clear();
            search.matches.clear();
            search.currentIndex = 0;
            needFullRedraw = true; // Search highlights need to be cleared
            break;
        case 't':
        {
            currentScheme = static_cast<ColorScheme::SchemeId>((static_cast<int>(currentScheme) + 1) % ColorScheme::COUNT);
            applyColorScheme(currentScheme, colours);

            // Schedule a transient status message for 3 seconds without blocking
            showTransientStatus(getColorSchemeStatusMessage(colours), 3000);

            needFullRedraw = true; // Redraw with new scheme and status message
        }
            break;
        case 'y':
            // Copy current selection to clipboard
            if (selected < visible.size())
            {
                const Node *selectedNode = visible[selected];
                json jsonData = reconstructJson(selectedNode);
                std::string jsonStr = jsonData.dump(2); // Pretty-print with 2-space indentation

                copyToClipboard(jsonStr);

                // Queue a transient status message (3s) without blocking input
                std::string message = getClipboardStatusMessage();
                showTransientStatus(message, 3000);
                needFullRedraw = true;
            }
            break;
        case '?':
            showHelp();
            needFullRedraw = true; // Help dialog changed the screen
            break;
        case KEY_RESIZE:
            // Handle window resize
            endwin();
            refresh();
            clear();
            needFullRedraw = true; // Window resized, need full redraw
            break;
        case 'q':
        case 'Q':
            running = false;
            break;
        default:
            break;
        }
    }
    // End curses mode
    endwin();
    // Free nodes
    // unique_ptr takes care of cleanup of the entire tree
    return 0;
}
