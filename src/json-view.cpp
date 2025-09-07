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
#include <string>
#include <vector>

// Color scheme for easy customization - modify these values to change colors
namespace ColorScheme
{
    // Foreground colors for each element type
    constexpr int NORMAL_FG = COLOR_WHITE;
    constexpr int SELECTION_FG = COLOR_BLACK;
    constexpr int SEARCH_MATCH_FG = COLOR_YELLOW;
    constexpr int SELECTION_MATCH_FG = COLOR_BLACK;
    constexpr int TREE_STRUCTURE_FG = COLOR_BLUE;
    constexpr int EXPAND_INDICATORS_FG = COLOR_MAGENTA;
    constexpr int STRING_VALUES_FG = COLOR_GREEN;
    constexpr int NUMBER_VALUES_FG = COLOR_GREEN;
    constexpr int BOOLEAN_VALUES_FG = COLOR_YELLOW;
    constexpr int NULL_VALUES_FG = COLOR_RED;
    constexpr int KEY_NAMES_FG = COLOR_CYAN;

    // Background colors (-1 means use default terminal background)
    constexpr int DEFAULT_BG = -1;
    constexpr int SELECTION_BG = COLOR_CYAN;
    constexpr int SELECTION_MATCH_BG = COLOR_GREEN;

    // Color pair indices (internal use only)
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

    constexpr int STATUS_BAR = SELECTION_BG_PAIR; // Status bar uses same as selection
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

// When true, render tree and icons using plain ASCII instead of Unicode
static bool asciiMode = false;

// A Node represents a single entry in the tree.  Each node holds a
// pointer into the underlying JSON document and a list of child nodes
// for objects and arrays.  Nodes store their key (for object
// properties) or index (for array elements) for display purposes.
struct Node
{
    const json *value = nullptr;  // pointer into the parsed JSON
    Node *parent = nullptr;       // parent node (null for roots)
    std::vector<std::unique_ptr<Node>> children; // child nodes (empty for leaves)
    std::string key;              // property name or array index
    bool expanded = false;        // true if children are visible
    bool isDummyRoot = false;     // true if this node represents a file
    bool isLastChild = false;     // true if this node is last among siblings
};

// Forward declarations
static std::unique_ptr<Node> buildTree(const json *j, const std::string &key,
                                       Node *parent, bool dummy);
static void collectVisible(const Node *node, std::vector<const Node *> &out);
static std::string buildPrefix(const Node *node);
static std::string shortenPath(const std::string &path, int maxWidth);
static std::string getTypeIcon(const Node *node);
static std::string getContentLabel(const Node *node, int maxWidth = 80);
static void expandAll(Node *node);
static void collapseAll(Node *node, bool keepRoot);
static void expandToLevel(Node *node, int level, int currentLevel = 0);
static std::string getClipboardStatusMessage();
static std::string base64Encode(const std::string &input);
static void copyToClipboard(const std::string &text);
static json reconstructJson(const Node *node);
static std::string formatFileSize(size_t size);
static void printFormattedJson(const json &j, int indent = 0);
static json parseJsonWithSpecialNumbers(const std::string &contents);

// Calculate the display width of a UTF-8 string (handles Unicode properly)
static int getDisplayWidth(const std::string &str)
{
    // Convert UTF-8 string to wide character string
    size_t len = str.length();
    wchar_t *wstr = new wchar_t[len + 1];

    // Convert UTF-8 to wide characters
    size_t result = mbstowcs(wstr, str.c_str(), len);
    if (result == (size_t)-1)
    {
        // Conversion failed, fall back to byte length
        delete[] wstr;
        return str.length();
    }
    wstr[result] = L'\0';

    // Get display width
    int width = wcswidth(wstr, result);
    delete[] wstr;

    // wcswidth returns -1 for unprintable characters, fall back to byte length
    return (width >= 0) ? width : str.length();
}

// Structure to hold file information for size tracking
struct FileInfo
{
    std::string filename;
    size_t fileSize;
    json document;
};

// Global map to store file sizes by filename for display purposes
static std::map<std::string, size_t> fileSizes;

// Structure used to hold the current search state.  When a search is
// active the term stores the case‑insensitive query, the matches
// contain pointers to nodes that match, and currentIndex marks the
// current selection within matches.
struct SearchState
{
    std::string term;          // lower‑cased search term
    bool searchKeys = true;    // true to search keys, false to search values
    bool searchValues = false; // true to search values, false to search keys
    std::vector<const Node *> matches;
    int currentIndex = 0;
};

// Structure describing clickable hint regions in the status bar
struct ClickHint
{
    char key;   // key to simulate when clicked
    int start;  // inclusive column start
    int end;    // exclusive column end
};

static std::vector<ClickHint> clickableHints;

// Forward declaration for getContentLabel with search state
static std::string getContentLabelWithSearch(const Node *node, const SearchState &search, int maxWidth = 80);

// Build a tree of Node objects mirroring the structure of a JSON
// document.  Each Node stores a pointer to the original JSON value.
static std::unique_ptr<Node> buildTree(const json *j, const std::string &key,
                                       Node *parent, bool dummy)
{
    auto node = std::make_unique<Node>();
    node->value = j;
    node->parent = parent;
    node->key = key;
    node->isDummyRoot = dummy;
    // Root nodes are expanded by default so the top‑level structure is visible.
    node->expanded = dummy;
    // Recursively create children for objects and arrays.  Primitive
    // values have no children.
    if (j->is_object())
    {
        node->children.reserve(j->size());
        for (auto it = j->begin(); it != j->end(); ++it)
        {
            const std::string &childKey = it.key();
            const json &childVal = it.value();
            auto child = buildTree(&childVal, childKey, node.get(), false);
            node->children.push_back(std::move(child));
        }
    }
    else if (j->is_array())
    {
        node->children.reserve(j->size());
        int idx = 0;
        for (auto it = j->begin(); it != j->end(); ++it, ++idx)
        {
            std::string childKey = "[" + std::to_string(idx) + "]";
            auto child = buildTree(&(*it), childKey, node.get(), false);
            node->children.push_back(std::move(child));
        }
    }
    // Mark last child in the list.  This information is used to draw
    // the tree branches correctly.
    for (size_t i = 0; i < node->children.size(); ++i)
    {
        node->children[i]->isLastChild = (i == node->children.size() - 1);
    }
    return node;
}

// Recursively collect all nodes that are currently visible.  A node is
// visible if it is a root or its parent is expanded.
static void collectVisible(const Node *node, std::vector<const Node *> &out)
{
    out.push_back(node);
    if (node->expanded)
    {
        for (const auto &child : node->children)
        {
            collectVisible(child.get(), out);
        }
    }
}

// Build the tree prefix for a node.  This string contains the
// vertical bar and branch characters needed to draw a proper tree.
static std::string buildPrefix(const Node *node)
{
    std::string prefix;
    const Node *cur = node;
    while (cur->parent != nullptr)
    {
        const Node *parent = cur->parent;
        // skip the dummy root (it has no parent) so its isLastChild flag
        // doesn’t affect vertical lines for deeper levels
        if (parent->parent != nullptr)
        {
            if (!parent->isLastChild)
            {
                prefix = std::string("│   ") + prefix;
            }
            else
            {
                prefix = std::string("    ") + prefix;
            }
        }
        cur = parent;
    }
    if (node->parent != nullptr)
    {
        prefix += node->isLastChild ? "└── " : "├── ";
    }
    return prefix;
}

// Shorten a file path to fit within maxWidth by replacing middle parts with "..."
static std::string shortenPath(const std::string &path, int maxWidth)
{
    if (getDisplayWidth(path) <= maxWidth)
    {
        return path;
    }

    // Find the filename (last component)
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos)
    {
        // No slash found, just truncate the string
        return path.substr(0, maxWidth - 3) + "...";
    }

    std::string filename = path.substr(lastSlash + 1);
    std::string directory = path.substr(0, lastSlash);

    // If filename alone is too long, truncate it
    if (getDisplayWidth(filename) > maxWidth - 4)
    { // -4 for ".../"
        return ".../" + filename.substr(0, maxWidth - 7) + "...";
    }

    // Calculate remaining space for directory
    int remainingSpace = maxWidth - getDisplayWidth(filename) - 1; // -1 for the slash

    if (getDisplayWidth(directory) <= remainingSpace)
    {
        return path; // Original fits
    }

    // Need to shorten directory part
    // Strategy: keep start and end of path, replace middle with "..."
    int prefixLen = std::max(1, remainingSpace / 3);
    int suffixLen = std::max(1, remainingSpace - prefixLen - 3); // -3 for "..."

    if (prefixLen + suffixLen + 3 >= getDisplayWidth(directory))
    {
        return path; // Would not actually shorten
    }

    std::string prefix = directory.substr(0, prefixLen);
    std::string suffix = directory.substr(directory.length() - suffixLen);

    return prefix + "..." + suffix + "/" + filename;
}

// Get the type icon for a node
static std::string getTypeIcon(const Node *node)
{
    const json *v = node->value;
    if (node->isDummyRoot)
    {
        return ""; // No icons for dummy roots
    }

    if (v->is_string())
        return "℀ ";
    else if (v->is_boolean())
        return v->get<bool>() ? "☒ " : "☐ ";
    else if (v->is_number())
        return "⅑ ";
    else if (v->is_null())
        return "⊘ ";
    else if (v->is_object() && v->empty())
        return "⁞ "; // Only for empty dictionaries
    else if (v->is_array())
        return ""; // No icon for arrays
    else if (v->is_object())
        return ""; // No icon for non-empty objects
    return "";
}

// Get the content without type icon
static std::string getContentLabel(const Node *node, int maxWidth)
{
    const json *v = node->value;
    if (node->isDummyRoot)
    {
        std::string type;
        if (v->is_object())
        {
            size_t count = v->size();
            type = "📦 dictionary, " + std::to_string(count) + (count == 1 ? " key" : " keys");
        }
        else if (v->is_array())
        {
            size_t count = v->size();
            type = "🗂️ list, " + std::to_string(count) + (count == 1 ? " item" : " items");
        }
        else if (v->is_string())
            type = "℀ string";
        else if (v->is_number())
            type = "⅑ number";
        else if (v->is_boolean())
            type = "☒ boolean";
        else if (v->is_null())
            type = "⊘ null";
        else
            type = "📄 value";

        // Add file size information from stored file sizes
        auto it = fileSizes.find(node->key);
        if (it != fileSizes.end())
        {
            std::string fileSizeStr = formatFileSize(it->second);
            type += ", " + fileSizeStr;
        }

        // Shorten the filename for display
        std::string shortKey = shortenPath(node->key, maxWidth - getDisplayWidth(type) - 4); // -4 for " ()"
        return shortKey + " (" + type + ")";
    }

    // For objects and arrays, no icons for expandable items
    if (v->is_object())
    {
        size_t count = v->size();
        return node->key + " (dictionary, " + std::to_string(count) + (count == 1 ? " key)" : " keys)");
    }
    else if (v->is_array())
    {
        size_t count = v->size();
        std::string baseLabel = node->key + " (list, " + std::to_string(count) + (count == 1 ? " item)" : " items)");

        // Array previews are rendered directly in drawLine; return base label only
        return baseLabel;
    }
    else if (v->is_string())
    {
        // Escape quotes in string for readability
        std::string s = v->get<std::string>();
        // Replace control characters with escapes
        std::string out;
        for (char c : s)
        {
            if (c == '\\')
                out += "\\\\";
            else if (c == '\"')
                out += "\\\"";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            }
            else
                out += c;
        }
        return node->key + ": \"" + out + "\"";
    }
    else if (v->is_boolean())
    {
        return node->key + ": " + std::string(v->get<bool>() ? "true" : "false");
    }
    else if (v->is_number())
    {
        return node->key + ": " + v->dump();
    }
    else if (v->is_null())
    {
        return node->key + ": null";
    }
    else
    {
        // Fallback for other types
        return node->key;
    }
}

// Get the content label with search match information
static std::string getContentLabelWithSearch(const Node *node, const SearchState &search, int maxWidth)
{
    // For non-dummy root nodes, just use the original function
    if (!node->isDummyRoot)
    {
        return getContentLabel(node, maxWidth);
    }

    // For dummy root nodes, add search match count if search is active
    std::string baseLabel = getContentLabel(node, maxWidth);

    if (!search.term.empty() && !search.matches.empty())
    {
        // Count matches that belong to this dummy root's subtree
        int matchCount = 0;
        for (const Node *match : search.matches)
        {
            // Walk up the tree to find the root
            const Node *current = match;
            while (current->parent != nullptr)
            {
                current = current->parent;
            }
            if (current == node)
            {
                matchCount++;
            }
        }

        if (matchCount > 0)
        {
            // Find the closing parenthesis in the base label
            size_t closeParenPos = baseLabel.rfind(')');
            if (closeParenPos != std::string::npos)
            {
                // Insert the match count before the closing parenthesis
                std::string matchInfo = ", 🔍 " + std::to_string(matchCount) + (matchCount == 1 ? " match" : " matches");
                baseLabel.insert(closeParenPos, matchInfo);
            }
        }
    }

    return baseLabel;
}

// Generate a preview of array values for inline display
// (arrayPreview removed; previews are rendered directly in drawLine)

// Expand every branch of the given node.  Primitive leaves remain
// unchanged.  This function recurses through all descendants.
static void expandAll(Node *node)
{
    node->expanded = true;
    for (auto &child : node->children)
    {
        expandAll(child.get());
    }
}

// Collapse every branch of the given node.  When keepRoot is true the
// top‑level node remains expanded so the structure of the document is
// still visible.
static void collapseAll(Node *node, bool keepRoot)
{
    if (!node->isDummyRoot || !keepRoot)
    {
        node->expanded = false;
    }
    for (auto &child : node->children)
    {
        collapseAll(child.get(), false);
    }
}

// Expand nodes up to a specific nesting level.
// Level 0 means collapse all, level 1 means show only first level, etc.
static void expandToLevel(Node *node, int targetLevel, int currentLevel)
{
    // Special handling for level 0 - collapse everything
    if (targetLevel == 0)
    {
        // Collapse everything, including root nodes
        node->expanded = false;
        for (auto &child : node->children)
        {
            collapseAll(child.get(), false);
        }
        return;
    }

    // For dummy roots, don't change their expansion state, just process children
    if (node->isDummyRoot)
    {
        node->expanded = true; // Always expand dummy roots when target > 0
        for (auto &child : node->children)
        {
            expandToLevel(child.get(), targetLevel, 1); // Children of dummy root are at level 1
        }
        return;
    }

    // For regular nodes:
    if (currentLevel < targetLevel)
    {
        // We haven't reached the target depth yet - expand and recurse
        node->expanded = true;
        for (auto &child : node->children)
        {
            expandToLevel(child.get(), targetLevel, currentLevel + 1);
        }
    }
    else
    {
        // We're at or beyond the target depth - collapse
        node->expanded = false;
        for (auto &child : node->children)
        {
            collapseAll(child.get(), false);
        }
    }
}

// Recursively search for nodes matching the given search term.  Both
// keys and values can be searched.  The search term and the
// candidates are compared in lowercase to achieve case‑insensitive
// matching.  When searching values, primitive values are converted to
// their JSON string representation.
static void searchTree(const Node *node, const std::string &term,
                       bool searchKeys, bool searchValues,
                       std::vector<const Node *> &out)
{
    // Convert the node's key and value to lowercase strings
    if (!term.empty())
    {
        if (searchKeys)
        {
            std::string keyLower = node->key;
            std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
            if (keyLower.find(term) != std::string::npos)
            {
                out.push_back(node);
            }
        }
        if (!searchKeys && searchValues)
        {
            // Convert value to a displayable string
            std::string val;
            const json *v = node->value;
            if (v->is_string())
                val = v->get<std::string>();
            else if (v->is_boolean())
                val = (v->get<bool>() ? "true" : "false");
            else if (v->is_number())
                val = v->dump();
            else if (v->is_null())
                val = "null";
            else if (v->is_object())
                val = "dictionary";
            else if (v->is_array())
                val = "list";
            else
                val = v->dump();
            std::string valLower = val;
            std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
            if (valLower.find(term) != std::string::npos)
            {
                out.push_back(node);
            }
        }
    }
    // Continue search through children
    for (const auto &child : node->children)
    {
        searchTree(child.get(), term, searchKeys, searchValues, out);
    }
}

// Expand all ancestors of the given node so that it becomes visible.
static void expandPath(Node *node)
{
    Node *cur = node->parent;
    while (cur)
    {
        cur->expanded = true;
        cur = cur->parent;
    }
}

// Base64 encoding for OSC 52 clipboard support
static std::string base64Encode(const std::string &input)
{
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : input)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4)
        encoded.push_back('=');
    return encoded;
}

// Check if an executable exists in PATH
// Check if OSC 52 clipboard sequences are likely to be supported
static bool osc52Likely()
{
    // Check for explicit opt-out
    const char *noOsc52 = std::getenv("NO_OSC52");
    if (noOsc52 && *noOsc52)
        return false;

    const char *term = std::getenv("TERM");
    if (!term)
        return false;

    std::string termStr(term);

    // Terminals that definitely don't support OSC 52
    if (termStr == "dumb" || termStr == "linux")
        return false;

    // Common terminals/muxers that support OSC 52 (including tmux!)
    return termStr.find("xterm") != std::string::npos ||
           termStr.find("tmux") != std::string::npos ||
           termStr.find("screen") != std::string::npos ||
           termStr.find("rxvt") != std::string::npos ||
           termStr.find("alacritty") != std::string::npos ||
           termStr.find("foot") != std::string::npos ||
           termStr.find("kitty") != std::string::npos ||
           termStr.find("wezterm") != std::string::npos;
}

// Check if the current terminal supports clipboard operations via OSC 52
// Get a descriptive message about clipboard support status
static std::string getClipboardStatusMessage()
{
    if (osc52Likely())
    {
        return "JSON copied to clipboard!";
    }
    if (std::getenv("TMUX") && !osc52Likely())
    {
        return "Clipboard not supported - tmux needs OSC 52 configuration";
    }
    return "Clipboard not supported by this terminal";
}

// Copy text to clipboard using OSC 52 escape sequence
static void copyToClipboard(const std::string &text)
{
    if (!osc52Likely())
        return;

    // Limit payload size to prevent issues with muxers/terminals
    constexpr size_t maxOsc52Payload = 100000;
    std::string encoded = base64Encode(text);

    if (encoded.size() <= maxOsc52Payload)
    {
        // Try to write to /dev/tty first, fall back to stdout
        FILE *out = std::fopen("/dev/tty", "w");
        if (!out && isatty(fileno(stdout)))
        {
            out = stdout;
        }

        if (out)
        {
            // Use BEL terminator instead of ST for better compatibility
            std::fprintf(out, "\033]52;c;%s\a", encoded.c_str());
            std::fflush(out);
            if (out != stdout)
            {
                std::fclose(out);
            }
        }
    }
}

// Reconstruct JSON from a node and its children
static json reconstructJson(const Node *node)
{
    if (node->isDummyRoot)
    {
        // For dummy root, return the actual JSON value
        return *node->value;
    }

    // Return the JSON value of this node
    return *node->value;
}

// Format file size in human-readable units
static std::string formatFileSize(size_t size)
{
    const char *units[] = {"Bytes", "KB", "MB", "GB", "TB"};
    const size_t unitCount = sizeof(units) / sizeof(units[0]);

    double fileSize = static_cast<double>(size);
    size_t unitIndex = 0;

    while (fileSize >= 1024.0 && unitIndex < unitCount - 1)
    {
        fileSize /= 1024.0;
        unitIndex++;
    }

    // Format with appropriate precision
    std::ostringstream oss;
    if (unitIndex == 0)
    {
        // For bytes, show exact count
        oss << static_cast<size_t>(fileSize) << " " << units[unitIndex];
    }
    else if (fileSize < 10.0)
    {
        // For smaller values, show one decimal place
        oss.precision(1);
        oss << std::fixed << fileSize << " " << units[unitIndex];
    }
    else
    {
        // For larger values, show whole numbers
        oss << static_cast<int>(fileSize + 0.5) << " " << units[unitIndex];
    }

    return oss.str();
}

// Pretty-print JSON preserving NaN/Infinity literals
static void printFormattedJson(const json &j, int indent)
{
    std::string pad(indent, ' ');
    switch (j.type())
    {
    case json::value_t::object:
        if (j.empty())
        {
            std::cout << "{}";
            return;
        }
        std::cout << "{\n";
        for (auto it = j.cbegin(); it != j.cend(); ++it)
        {
            std::cout << std::string(indent + 2, ' ')
                      << json(it.key()).dump() << ": ";
            printFormattedJson(it.value(), indent + 2);
            if (std::next(it) != j.cend())
                std::cout << ",";
            std::cout << "\n";
        }
        std::cout << pad << "}";
        break;
    case json::value_t::array:
        if (j.empty())
        {
            std::cout << "[]";
            return;
        }
        std::cout << "[\n";
        for (size_t i = 0; i < j.size(); ++i)
        {
            std::cout << std::string(indent + 2, ' ');
            printFormattedJson(j[i], indent + 2);
            if (i + 1 < j.size())
                std::cout << ",";
            std::cout << "\n";
        }
        std::cout << pad << "]";
        break;
    case json::value_t::string:
        std::cout << j.dump();
        break;
    case json::value_t::boolean:
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        std::cout << j.dump();
        break;
    case json::value_t::number_float:
    {
        double d = j.get<double>();
        if (std::isnan(d))
            std::cout << "NaN";
        else if (std::isinf(d))
            std::cout << (d > 0 ? "Infinity" : "-Infinity");
        else
            std::cout << j.dump();
        break;
    }
    case json::value_t::null:
        std::cout << "null";
        break;
    default:
        std::cout << j.dump();
        break;
    }
}

// Replace placeholder strings with special floating-point values
static void replaceSpecialStrings(json &j)
{
    if (j.is_string())
    {
        auto &s = j.get_ref<json::string_t &>();
        if (s == "__JSON_VIEW_NaN__")
            j = std::numeric_limits<double>::quiet_NaN();
        else if (s == "__JSON_VIEW_INF__")
            j = std::numeric_limits<double>::infinity();
        else if (s == "__JSON_VIEW_NEG_INF__")
            j = -std::numeric_limits<double>::infinity();
    }
    else if (j.is_object())
    {
        for (auto &el : j.items())
            replaceSpecialStrings(el.value());
    }
    else if (j.is_array())
    {
        for (auto &el : j)
            replaceSpecialStrings(el);
    }
}

// Parse JSON while preserving NaN/Infinity literals by using placeholders
static json parseJsonWithSpecialNumbers(const std::string &contents)
{
    std::string processed;
    processed.reserve(contents.size());
    bool inString = false;
    for (size_t i = 0; i < contents.size();)
    {
        char c = contents[i];
        if (inString)
        {
            processed.push_back(c);
            if (c == '\\')
            {
                ++i;
                if (i < contents.size())
                    processed.push_back(contents[i]);
                ++i;
            }
            else if (c == '"')
            {
                inString = false;
                ++i;
            }
            else
            {
                ++i;
            }
        }
        else
        {
            if (c == '"')
            {
                inString = true;
                processed.push_back(c);
                ++i;
            }
            else if (contents.compare(i, 3, "NaN") == 0)
            {
                processed += "\"__JSON_VIEW_NaN__\"";
                i += 3;
            }
            else if (contents.compare(i, 8, "Infinity") == 0)
            {
                processed += "\"__JSON_VIEW_INF__\"";
                i += 8;
            }
            else if (contents.compare(i, 9, "-Infinity") == 0)
            {
                processed += "\"__JSON_VIEW_NEG_INF__\"";
                i += 9;
            }
            else
            {
                processed.push_back(c);
                ++i;
            }
        }
    }

    json j = json::parse(processed);
    replaceSpecialStrings(j);
    return j;
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
    std::cout << "  " << progName << " [--parse-only|--validate] [--no-mouse] [--ascii] [file1.json] [file2.json] ...\n";
    std::cout << "  cat data.json | " << progName << " [--parse-only|--validate] [--no-mouse] [--ascii]\n\n";
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
    std::cout << "  y         Copy selected JSON to clipboard\n";
    std::cout << "  ?         Show help screen\n";
    std::cout << "  q         Quit the program\n";
    std::cout << "  Mouse     Click to select, click left of label or double-click to expand/collapse, click footer hints, click help screen to close\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "  -V, --version     Show version information\n";
    std::cout << "  -p, --parse-only  Parse input and pretty-print JSON then exit\n";
    std::cout << "      --validate    Validate JSON input and exit with status\n";
    std::cout << "      --no-mouse    Disable mouse support\n";
    std::cout << "      --ascii       Use ASCII tree/indicator characters (or set JSON_VIEW_ASCII=1)\n\n";
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

    // Enable UTF-8 support in ncurses
    if (has_colors())
    {
        start_color();
    }
    // Initialise colours if available
    bool colours = has_colors();
    if (colours)
    {
        start_color();
        use_default_colors();
        // Initialize color pairs using the defined color scheme
        init_pair(ColorScheme::NORMAL_TEXT, ColorScheme::NORMAL_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::SELECTION_BG_PAIR, ColorScheme::SELECTION_FG, ColorScheme::SELECTION_BG);
        init_pair(ColorScheme::SEARCH_MATCH, ColorScheme::SEARCH_MATCH_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::SELECTION_MATCH_BG_PAIR, ColorScheme::SELECTION_MATCH_FG, ColorScheme::SELECTION_MATCH_BG);
        init_pair(ColorScheme::TREE_STRUCTURE, ColorScheme::TREE_STRUCTURE_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::EXPAND_INDICATORS, ColorScheme::EXPAND_INDICATORS_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::STRING_VALUES, ColorScheme::STRING_VALUES_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::NUMBER_VALUES, ColorScheme::NUMBER_VALUES_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::BOOLEAN_VALUES, ColorScheme::BOOLEAN_VALUES_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::NULL_VALUES, ColorScheme::NULL_VALUES_FG, ColorScheme::DEFAULT_BG);
        init_pair(ColorScheme::KEY_NAMES, ColorScheme::KEY_NAMES_FG, ColorScheme::DEFAULT_BG);
    }

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
        case 'y':
            // Copy current selection to clipboard
            if (selected < visible.size())
            {
                const Node *selectedNode = visible[selected];
                json jsonData = reconstructJson(selectedNode);
                std::string jsonStr = jsonData.dump(2); // Pretty-print with 2-space indentation

                int rows, cols;
                getmaxyx(stdscr, rows, cols);

                copyToClipboard(jsonStr);

                // Show appropriate status message
                std::string message = getClipboardStatusMessage();
                mvprintw(rows - 1, 0, "%s", message.c_str());
                clrtoeol();
                refresh();

                // Wait briefly to show the message
                timeout(1000);
                getch();
                timeout(-1);           // Reset to blocking mode
                needFullRedraw = true; // Status line was overwritten, need full redraw
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
