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
#define Uses_TRadioButtons
#define Uses_TButton
#define Uses_TLabel
#define Uses_TSItem
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

static constexpr const char *kDeveloperName = "Dr. C. Klukas";

class JsonTNode : public TNode
{
public:
    const Node *jsonNode;
    JsonTNode *parent = nullptr;
    JsonTNode(const Node *n, TStringView text, JsonTNode *children = nullptr,
              JsonTNode *next = nullptr, Boolean exp = True)
        : TNode(text, children, next, exp), jsonNode(n) {}
};

class JsonOutline : public TOutline
{
public:
    JsonOutline(TRect r, TScrollBar *h, TScrollBar *v, JsonTNode *aRoot)
        : TOutline(r, h, v, aRoot), root(aRoot) {}

    JsonTNode *root;

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

    virtual void handleEvent(TEvent &event) override
    {
        if (event.what == evMouseDown && (event.mouse.buttons & mbLeftButton))
        {
            int clickX = event.mouse.where.x;
            TOutline::handleEvent(event);
            JsonTNode *node = focusedNode();
            if (node)
            {
                int depth = 0;
                for (const Node *p = node->jsonNode; p && p->parent; p = p->parent)
                    ++depth;
                int prefixWidth = depth * 2 + 2;
                if (clickX < prefixWidth)
                {
                    node->expanded = node->expanded ? False : True;
                    const_cast<Node *>(node->jsonNode)->expanded = node->expanded;
                    update();
                    drawView();
                }
            }
        }
        else if (event.what == evKeyDown)
        {
            JsonTNode *node = focusedNode();
            switch (event.keyDown.keyCode)
            {
            case kbLeft:
                if (node)
                {
                    if (node->expanded && node->childList)
                    {
                        node->expanded = False;
                        const_cast<Node *>(node->jsonNode)->expanded = false;
                        update();
                        drawView();
                    }
                    else if (node->parent)
                        focusNode(node->parent);
                }
                clearEvent(event);
                break;
            case kbRight:
                if (node)
                {
                    if (!node->expanded && node->childList)
                    {
                        node->expanded = True;
                        const_cast<Node *>(node->jsonNode)->expanded = true;
                        update();
                        drawView();
                    }
                    else if (node->childList)
                        focusNode(static_cast<JsonTNode *>(node->childList));
                }
                clearEvent(event);
                break;
            case kbHome:
                focusNode(static_cast<JsonTNode *>(getNode(0)));
                clearEvent(event);
                break;
            case kbEnd:
            {
                JsonTNode *last = nullptr;
                forEach([](TOutlineViewer *, TNode *n, int, int, long, ushort, void *arg) -> Boolean {
                    *static_cast<JsonTNode **>(arg) = static_cast<JsonTNode *>(n);
                    return False;
                }, &last);
                if (last)
                    focusNode(last);
                clearEvent(event);
                break;
            }
            default:
                TOutline::handleEvent(event);
            }
        }
        else
            TOutline::handleEvent(event);
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
    void updateStatusBar();
    void syncOutlineExpansion();
};

static const ushort cmFind = 1000;
static const ushort cmFindNext = 1001;
static const ushort cmFindPrev = 1002;
static const ushort cmAbout = 1003;
static const ushort cmLevel0 = 1100;
static const ushort cmLevel1 = 1101;
static const ushort cmLevel2 = 1102;
static const ushort cmLevel3 = 1103;
static const ushort cmLevel4 = 1104;
static const ushort cmLevel5 = 1105;
static const ushort cmLevel6 = 1106;
static const ushort cmLevel7 = 1107;
static const ushort cmLevel8 = 1108;
static const ushort cmLevel9 = 1109;

class JsonStatusLine : public TStatusLine
{
public:
    JsonStatusLine(TRect r) : TStatusLine(r, *new TStatusDef(0, 0xFFFF, nullptr)) { setSearchState(SearchState()); }

    void setSearchState(const SearchState &s)
    {
        disposeItems(items);
        TStatusItem *chain = nullptr;
        if (s.matches.empty())
        {
            auto *i1 = new TStatusItem("~F2~ Open", kbF2, cmOpen);
            auto *i2 = new TStatusItem("~F3~ Find", kbF3, cmFind);
            auto *i3 = new TStatusItem("~Ctrl+Q~ Quit", kbCtrlQ, cmQuit);
            i1->next = i2;
            i2->next = i3;
            chain = i1;
        }
        else
        {
            std::string info = "search '" + s.term + "' " +
                               std::to_string(s.currentIndex + 1) + "/" +
                               std::to_string(s.matches.size());
            auto *i1 = new TStatusItem(info.c_str(), kbNoKey, 0);
            auto *i2 = new TStatusItem("~F3~ Next", kbF3, cmFindNext);
            auto *i3 = new TStatusItem("~Shift+F3~ Prev", kbShiftF3, cmFindPrev);
            auto *i4 = new TStatusItem("~Ctrl+Q~ Quit", kbCtrlQ, cmQuit);
            i1->next = i2;
            i2->next = i3;
            i3->next = i4;
            chain = i1;
        }
        items = chain;
        defs->items = items;
        drawView();
    }

private:
    void disposeItems(TStatusItem *item)
    {
        while (item)
        {
            TStatusItem *next = item->next;
            delete item;
            item = next;
        }
    }
};

static void syncExpanded(JsonTNode *n)
{
    if (!n)
        return;
    n->expanded = n->jsonNode->expanded ? True : False;
    for (JsonTNode *c = static_cast<JsonTNode *>(n->childList); c; c = static_cast<JsonTNode *>(c->next))
        syncExpanded(c);
}

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
    if (event.what == evKeyDown)
    {
        if ((event.keyDown.controlKeyState & kbCtrlShift) &&
            event.keyDown.keyCode >= '0' && event.keyDown.keyCode <= '9')
        {
            if (root)
            {
                int level = event.keyDown.keyCode - '0';
                expandToLevel(root.get(), level, 0);
                syncOutlineExpansion();
            }
            clearEvent(event);
            return;
        }
    }
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
                updateStatusBar();
            }
            break;
        case cmLevel0:
        case cmLevel1:
        case cmLevel2:
        case cmLevel3:
        case cmLevel4:
        case cmLevel5:
        case cmLevel6:
        case cmLevel7:
        case cmLevel8:
        case cmLevel9:
            if (root)
            {
                int level = event.message.command - cmLevel0;
                expandToLevel(root.get(), level, 0);
                syncOutlineExpansion();
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
    updateStatusBar();
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
    updateStatusBar();
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
            std::vector<JsonTNode *> createdChildren;
            for (const auto &c : n->children)
            {
                JsonTNode *child = (*this)(c.get());
                createdChildren.push_back(child);
                if (!firstChild)
                    firstChild = child;
                else
                    prev->next = child;
                prev = child;
            }
            std::string label = getContentLabel(n);
            auto node = new JsonTNode(n, label, firstChild, nullptr, n->expanded ? True : False);
            for (auto *child : createdChildren)
                child->parent = node;
            map[n] = node;
            return node;
        }
    } builder{nodeMap};

    JsonTNode *tvRoot = builder(root.get());

    TRect r = deskTop->getExtent();
    r.grow(-2, -2);
    std::string title = root->key;
    size_t pos = title.find_last_of("/\\");
    if (pos != std::string::npos)
        title = title.substr(pos + 1);
    auto *win = new TWindow(r, title.c_str(), wnNoNumber);
    win->flags |= wfGrow;
    deskTop->insert(win);

    TRect c = win->getExtent();
    auto *sbH = new TScrollBar(TRect(1, c.b.y - 1, c.b.x - 1, c.b.y));
    sbH->growMode = gfGrowHiX;
    auto *sbV = new TScrollBar(TRect(c.b.x - 1, 1, c.b.x, c.b.y - 1));
    sbV->growMode = gfGrowHiY;
    auto *view = new JsonOutline(TRect(1, 1, c.b.x - 1, c.b.y - 1), sbH, sbV, tvRoot);
    view->growMode = gfGrowHiX | gfGrowHiY;
    win->insert(sbH);
    win->insert(sbV);
    win->insert(view);
    outline = view;
    outline->update();
}

void JsonViewApp::syncOutlineExpansion()
{
    if (!outline)
        return;
    syncExpanded(outline->root);
    outline->update();
    outline->drawView();
}

void JsonViewApp::updateStatusBar()
{
    static_cast<JsonStatusLine *>(statusLine)->setSearchState(search);
}

void JsonViewApp::doSearch(bool newTerm)
{
    if (!outline)
        return;
    if (newTerm)
    {
        struct SearchDialogData
        {
            char term[256];
            ushort mode;
        } data{{""}, 0};
        TDialog *d = new TDialog(TRect(0, 0, 40, 12), "Search");
        d->options |= ofCentered;
        auto *il = new TInputLine(TRect(3, 3, 35, 4), 255);
        d->insert(il);
        d->insert(new TLabel(TRect(2, 2, 12, 3), "~T~erm:", il));
        auto *rb = new TRadioButtons(TRect(3, 5, 35, 8),
                                     new TSItem("~K~eys",
                                                new TSItem("~V~alues",
                                                           new TSItem("~B~oth", nullptr))));
        d->insert(rb);
        d->insert(new TButton(TRect(9, 9, 19, 11), "O~K~", cmOK, bfDefault));
        d->insert(new TButton(TRect(21, 9, 31, 11), "Cancel", cmCancel, bfNormal));
        if (executeDialog(d, &data) != cmCancel)
        {
            search.matches.clear();
            search.term = data.term;
            search.searchKeys = (data.mode != 1);
            search.searchValues = (data.mode != 0);
            searchTree(root.get(), search.term, search.searchKeys, search.searchValues, search.matches);
            search.currentIndex = 0;
        }
        else
            return;
    }
    if (search.matches.empty())
    {
        messageBox("No matches", mfOKButton);
        updateStatusBar();
        return;
    }
    const Node *n = search.matches[search.currentIndex];
    for (const Node *p = n; p; p = p->parent)
    {
        auto it = nodeMap.find(p);
        if (it != nodeMap.end())
        {
            it->second->expanded = True;
            const_cast<Node *>(p)->expanded = true;
        }
    }
    outline->update();
    outline->focusNode(nodeMap[n]);
    if (!newTerm)
        search.currentIndex = (search.currentIndex + 1) % search.matches.size();
    updateStatusBar();
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
                            *new TMenuItem("~O~pen\tF2", cmOpen, kbF2, hcNoContext) +
                            *new TMenuItem("~C~lose\tF4", cmClose, kbF4, hcNoContext) +
                            newLine() +
                            *new TMenuItem("E~x~it\tAlt+X", cmQuit, kbAltX, hcNoContext) +
                        *new TSubMenu("~E~dit", hcNoContext) +
                            *new TMenuItem("~C~opy\tCtrl+C", cmCopy, kbCtrlC, hcNoContext) +
                        *new TSubMenu("~S~earch", hcNoContext) +
                            *new TMenuItem("~F~ind\tCtrl+F", cmFind, kbCtrlF, hcNoContext) +
                            *new TMenuItem("Find ~N~ext\tF3", cmFindNext, kbF3, hcNoContext) +
                            *new TMenuItem("Find ~P~rev\tShift+F3", cmFindPrev, kbShiftF3, hcNoContext) +
                        *new TSubMenu("~V~iew", hcNoContext) +
                            *new TMenuItem("Level ~0~\tAlt+0", cmLevel0, kbAlt0, hcNoContext) +
                            *new TMenuItem("Level ~1~\tAlt+1", cmLevel1, kbAlt1, hcNoContext) +
                            *new TMenuItem("Level ~2~\tAlt+2", cmLevel2, kbAlt2, hcNoContext) +
                            *new TMenuItem("Level ~3~\tAlt+3", cmLevel3, kbAlt3, hcNoContext) +
                            *new TMenuItem("Level ~4~\tAlt+4", cmLevel4, kbAlt4, hcNoContext) +
                            *new TMenuItem("Level ~5~\tAlt+5", cmLevel5, kbAlt5, hcNoContext) +
                            *new TMenuItem("Level ~6~\tAlt+6", cmLevel6, kbAlt6, hcNoContext) +
                            *new TMenuItem("Level ~7~\tAlt+7", cmLevel7, kbAlt7, hcNoContext) +
                            *new TMenuItem("Level ~8~\tAlt+8", cmLevel8, kbAlt8, hcNoContext) +
                            *new TMenuItem("Level ~9~\tAlt+9", cmLevel9, kbAlt9, hcNoContext) +
                        *new TSubMenu("~H~elp", hcNoContext) +
                            *new TMenuItem("~A~bout\tF1", cmAbout, kbF1, hcNoContext));
}

TStatusLine *JsonViewApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    return new JsonStatusLine(r);
}

int main(int argc, char **argv)
{
    JsonViewApp app(argc, argv);
    app.run();
    return 0;
}

