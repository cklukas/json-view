#include "json_view_core.hpp"

#define Uses_TApplication
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenu
#define Uses_TSubMenu
#define Uses_TScroller
#define Uses_TMenuItem
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TOutline
#define Uses_TScrollBar
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_MsgBox
#include <tvision/tv.h>

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>

static constexpr const char *kDeveloperName = "json-view developers";

class JsonTNode : public TNode
{
public:
    const Node *jsonNode;
    JsonTNode(const Node *n, TStringView text, JsonTNode *children = nullptr,
              JsonTNode *next = nullptr, Boolean exp = True)
        : TNode(text, children, next, exp), jsonNode(n) {}
};

class JsonOutline : public TOutline
{
public:
    using TOutline::TOutline;

    JsonTNode *focusedNode()
    {
        return static_cast<JsonTNode *>(getNode(foc));
    }

    void focusNode(JsonTNode *target)
    {
        struct Finder
        {
            JsonTNode *target;
            int index = 0;
            int found = -1;
        } finder{target};

        forEach([](TOutlineViewer *, TNode *node, int, int pos, long, ushort, void *arg) -> Boolean {
            auto &f = *static_cast<Finder *>(arg);
            if (node == f.target)
            {
                f.found = f.index;
                return True;
            }
            ++f.index;
            return False;
        }, &finder);

        if (finder.found >= 0)
        {
            foc = finder.found;
            scrollTo(0, finder.found);
            drawView();
            focused(finder.found);
        }
    }
};

class JsonViewApp : public TApplication
{
public:
    JsonViewApp(int argc, char **argv);

    virtual void handleEvent(TEvent &event);
    virtual void idle();
    static TMenuBar *initMenuBar(TRect r);
    static TStatusLine *initStatusLine(TRect r);

private:
    bool loadFile(const std::string &name);
    std::unique_ptr<Node> root;
    std::unordered_map<const Node *, JsonTNode *> nodeMap;
    JsonOutline *outline = nullptr;
    SearchState search;

    void openFile();
    void closeFile();
    void rebuildOutline();
    void doSearch(bool newTerm);
    void copySelection();
};

static const ushort cmFind = 1000;
static const ushort cmFindNext = 1001;
static const ushort cmFindPrev = 1002;
static const ushort cmAbout = 1003;

JsonViewApp::JsonViewApp(int argc, char **argv)
    : TProgInit(&JsonViewApp::initStatusLine, &JsonViewApp::initMenuBar, &TApplication::initDeskTop),
      TApplication()
{
    for (int i = 1; i < argc; ++i)
        loadFile(argv[i]);
}

void JsonViewApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    if (event.what == evCommand)
    {
        switch (event.message.command)
        {
        case cmOpen:
            openFile();
            break;
        case cmClose:
            closeFile();
            break;
        case cmCopy:
            copySelection();
            break;
        case cmFind:
            doSearch(true);
            break;
        case cmFindNext:
            doSearch(false);
            break;
        case cmFindPrev:
            if (!search.matches.empty())
            {
                search.currentIndex = (search.currentIndex - 1 + search.matches.size()) % search.matches.size();
                outline->focusNode(nodeMap[search.matches[search.currentIndex]]);
            }
            break;
        case cmAbout:
        {
            std::string msg = std::string("json-view-app ") + JSON_VIEW_VERSION +
                              "\nDeveloper: " + kDeveloperName;
            messageBox(msg.c_str(), mfInformation | mfOKButton);
            break;
        }
        default:
            return;
        }
        clearEvent(event);
    }
}

void JsonViewApp::idle()
{
    TApplication::idle();
}

void JsonViewApp::openFile()
{
    const size_t maxLen = 1024;
    char name[maxLen];
    name[0] = '\0';
    if (executeDialog(new TFileDialog("*.json", "Open JSON", "~N~ame", fdOpenButton, 1), name) != cmCancel)
        loadFile(name);
}

bool JsonViewApp::loadFile(const std::string &name)
{
    std::ifstream in(name);
    if (!in)
    {
        messageBox("Could not open file", mfError | mfOKButton);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    json j = parseJsonWithSpecialNumbers(ss.str());
    fileSizes.clear();
    fileSizes[name] = ss.str().size();
    root = buildTree(&j, name, nullptr, true);
    search = SearchState();
    rebuildOutline();
    return true;
}

void JsonViewApp::closeFile()
{
    if (outline)
    {
        deskTop->remove(outline->owner);
        outline = nullptr;
    }
    root.reset();
    nodeMap.clear();
    search = SearchState();
}

void JsonViewApp::rebuildOutline()
{
    nodeMap.clear();
    if (outline)
    {
        deskTop->remove(outline->owner);
        outline = nullptr;
    }
    if (!root)
        return;

    struct Builder
    {
        std::unordered_map<const Node *, JsonTNode *> &map;
        JsonTNode *operator()(const Node *n)
        {
            JsonTNode *firstChild = nullptr;
            JsonTNode *prev = nullptr;
            for (const auto &c : n->children)
            {
                JsonTNode *child = (*this)(c.get());
                if (!firstChild)
                    firstChild = child;
                else
                    prev->next = child;
                prev = child;
            }
            std::string label = getContentLabel(n);
            auto node = new JsonTNode(n, label, firstChild, nullptr, n->expanded ? True : False);
            map[n] = node;
            return node;
        }
    } builder{nodeMap};

    JsonTNode *tvRoot = nullptr;
    if (root && !root->children.empty())
        tvRoot = builder(root->children[0].get());
    else
        tvRoot = builder(root.get());

    TRect r = deskTop->getExtent();
    r.grow(-2, -2);
    auto *win = new TWindow(r, "json", wnNoNumber);
    win->flags |= wfGrow;
    TRect c = win->getExtent();
    auto *sbH = new TScrollBar(TRect(1, c.b.y - 1, c.b.x - 1, c.b.y));
    auto *sbV = new TScrollBar(TRect(c.b.x - 1, 1, c.b.x, c.b.y - 1));
    auto *view = new JsonOutline(TRect(1, 1, c.b.x - 1, c.b.y - 1), sbH, sbV, tvRoot);
    win->insert(view);
    win->insert(sbH);
    win->insert(sbV);
    outline = view;
    deskTop->insert(win);
}

void JsonViewApp::doSearch(bool newTerm)
{
    if (!outline)
        return;
    if (newTerm)
    {
        char buf[256] = "";
        if (inputBox("Search", "Search for:", buf, 255) != cmCancel)
        {
            search.matches.clear();
            search.term = buf;
            search.searchKeys = true;
            search.searchValues = false;
            searchTree(root.get(), search.term, search.searchKeys, search.searchValues, search.matches);
            search.currentIndex = 0;
        }
        else
            return;
    }
    if (search.matches.empty())
    {
        messageBox("No matches", mfOKButton);
        return;
    }
    const Node *n = search.matches[search.currentIndex];
    for (const Node *p = n; p; p = p->parent)
    {
        auto it = nodeMap.find(p);
        if (it != nodeMap.end())
            it->second->expanded = True;
    }
    outline->update();
    outline->focusNode(nodeMap[n]);
    if (!newTerm)
        search.currentIndex = (search.currentIndex + 1) % search.matches.size();
}

void JsonViewApp::copySelection()
{
    if (!outline)
        return;
    JsonTNode *n = outline->focusedNode();
    if (!n)
        return;
    json j = reconstructJson(n->jsonNode);
    copyToClipboard(j.dump());
    messageBox(getClipboardStatusMessage().c_str(), mfOKButton);
}

TMenuBar *JsonViewApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    return new TMenuBar(r,
                        *new TSubMenu("~F~ile", hcNoContext) +
                            *new TMenuItem("~O~pen", cmOpen, kbCtrlO, hcNoContext) +
                            *new TMenuItem("~C~lose", cmClose, kbCtrlW, hcNoContext) +
                            newLine() +
                            *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext) +
                        *new TSubMenu("~E~dit", hcNoContext) +
                            *new TMenuItem("~C~opy", cmCopy, kbCtrlC, hcNoContext) +
                        *new TSubMenu("~S~earch", hcNoContext) +
                            *new TMenuItem("~F~ind", cmFind, kbCtrlF, hcNoContext) +
                            *new TMenuItem("Find ~N~ext", cmFindNext, kbF3, hcNoContext) +
                            *new TMenuItem("Find ~P~rev", cmFindPrev, kbShiftF3, hcNoContext) +
                        *new TSubMenu("~H~elp", hcNoContext) +
                            *new TMenuItem("~A~bout", cmAbout, kbNoKey, hcNoContext));
}

TStatusLine *JsonViewApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    return new TStatusLine(r,
                           *new TStatusDef(0, 0xFFFF) +
                               *new TStatusItem("~F2~ Open", kbF2, cmOpen) +
                               *new TStatusItem("~F3~ Find", kbF3, cmFind) +
                               *new TStatusItem("~Ctrl+Q~ Quit", kbCtrlQ, cmQuit));
}

int main(int argc, char **argv)
{
    JsonViewApp app(argc, argv);
    app.run();
    return 0;
}

