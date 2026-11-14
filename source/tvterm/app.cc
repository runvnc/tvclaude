#define Uses_TMenuBar
#define Uses_TSubMenu
#define Uses_TMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TButton
#define Uses_TListViewer
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TDrawBuffer
#define Uses_TFrame
#define Uses_MsgBox
#include <tvision/tv.h>

#include "app.h"
#include "cmds.h"
#include "desk.h"
#include "wnd.h"
#include "apputil.h"
#include "history.h"
#include <tvterm/termctrl.h>
#include <tvterm/vtermemu.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <algorithm>
#include <sys/stat.h>

TCommandSet TVTermApp::tileCmds = []()
{
    TCommandSet ts;
    ts += cmTile;
    ts += cmTileCols;
    ts += cmTileRows;
    ts += cmCascade;
    return ts;
}();

// Claude's slash commands, as menu entries spread over the menus where
// they fit. Each types its command into the Claude window in front.
static const struct { const char *label; const char *command; } claudeCommands[] =
{
    {"~E~xport conversation...", "/export"},    // File
    {"~U~ndo (rewind)...", "/rewind"},          // Edit
    {"~C~opy reply", "/copy"},
    {"New ~c~onversation", "/clear"},           // Session
    {"C~o~mpact", "/compact"},
    {"Con~t~ext", "/context"},
    {"Re~v~iew changes", "/code-review"},
    {"~M~odel...", "/model"},                   // Options
    {"~P~ermissions...", "/permissions"},
    {"~C~onfig...", "/config"},
    {"~U~sage", "/usage"},                      // Info
    {"S~t~ats", "/stats"},
    {"Claude ~s~tatus", "/status"},             // Help
};

static TMenuItem &slashItem(const char *command)
{
    for (size_t i = 0; i < std::size(claudeCommands); ++i)
        if (strcmp(claudeCommands[i].command, command) == 0)
            return *new TMenuItem(claudeCommands[i].label, ushort(cmClaude0 + i),
                                  kbNoKey, hcNoContext, command);
    abort();
}


static long long nowMs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static std::string currentDir()
{
    char buf[PATH_MAX];
    return getcwd(buf, sizeof(buf)) ? buf : "";
}

static std::string expandHome(const std::string &path)
{
    if (!path.empty() && path[0] == '~')
        if (const char *home = getenv("HOME"))
            return home + path.substr(1);
    return path;
}

static std::string baseName(const std::string &path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

static std::string clipText(const std::string &s, size_t width);

// ---------------------------------------------------------------------------
// Exact VGA colors. Turbo Vision's palette names the 16 text-mode colors by
// index, which each terminal paints from its own palette (often not the DOS
// one). Converting them to RGB makes the desktop look the same everywhere.

static TColorRGB vgaColor(int i)
{
    static const TColorRGB vga[16] =
    {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
        {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
        {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
        {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
    };
    return vga[i & 0xF];
}

// A text-mode attribute byte (background in the high nibble) as exact RGB.
static TColorAttr vgaAttr(uchar bios)
{
    return TColorAttr(vgaColor(bios & 0xF), vgaColor(bios >> 4));
}

TPalette &TVTermApp::getPalette() const
{
    if (appPalette != apColor)
        return TApplication::getPalette();
    static TPalette rgb = [] {
        static const char bios[] = cpAppColor;
        enum { n = sizeof(bios) - 1 };
        static TColorAttr attrs[n];
        for (int i = 0; i < n; ++i)
            attrs[i] = vgaAttr((uchar) bios[i]);
        return TPalette(attrs, (ushort) n);
    }();
    return rgb;
}

// ---------------------------------------------------------------------------
// The session counter at the right end of the menu bar.

class SessionIndicator : public TView
{
public:

    int working {0}, waiting {0}, done {0};
    std::string workspace;
    int workspaceCount {1};

    SessionIndicator(const TRect &r) noexcept : TView(r)
    {
        growMode = gfGrowLoX | gfGrowHiX;
    }

    void draw() override
    {
        // The menu bar's colors: black on gray, the alarm in red.
        const TColorAttr normal = vgaAttr(0x70), alarm = vgaAttr(0x74), ok = vgaAttr(0x72);
        TDrawBuffer b;
        b.moveChar(0, ' ', normal, size.x);

        struct Piece { std::string text; TColorAttr attr; };
        std::vector<Piece> pieces;
        if (waiting)
            pieces.push_back({"! " + std::to_string(waiting) + " need you", alarm});
        if (working)
            pieces.push_back({"\xE2\x97\x8F " + std::to_string(working) + " working", normal});
        if (done)
            pieces.push_back({"\xE2\x9C\x93 " + std::to_string(done) + " done", ok});

        // The workspace on screen, left; the session counts, right.
        int left = 1;
        if (!workspace.empty())
        {
            std::string ws = "\xE2\x96\xA0 " + clipText(workspace, 16);  // ■
            if (workspaceCount > 1)
                ws += " (" + std::to_string(workspaceCount) + ")";
            left += b.moveStr(left, ws.c_str(), vgaAttr(0x71)) + 2;
        }

        int width = 0;
        for (auto &p : pieces)
            width += cstrlen(p.text.c_str()) + 2;
        int x = size.x - width;
        if (x < left)
            x = left;
        for (auto &p : pieces)
            x += b.moveStr(x, p.text.c_str(), p.attr) + 2;
        writeLine(0, 0, size.x, 1, b);
    }
};

// ---------------------------------------------------------------------------
// The Session List (Alt-0): one row per window, Enter goes to it.

class SessionListViewer : public TListViewer
{
public:

    std::vector<std::string> items;

    SessionListViewer(const TRect &r, TScrollBar *sb) noexcept :
        TListViewer(r, 1, nullptr, sb)
    {
    }

    void getText(char *dest, short item, short maxLen) override
    {
        strnzcpy(dest, items[item].c_str(), maxLen);
    }

    void selectItem(short item) override
    {
        TListViewer::selectItem(item);
        TEvent ev = {};
        ev.what = evCommand;
        ev.message.command = cmOK;
        putEvent(ev);
    }
};

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    std::vector<std::string> dirs;
    for (int i = 1; i < argc; ++i)
        dirs.push_back(argv[i]);
    // Turbo look inside the session windows: yellow on dark blue, and the
    // matching Claude Code theme.
    tvterm::setDefaultColors(TColorRGB(0xFF, 0xFF, 0x55), TColorRGB(0x00, 0x00, 0x80));
    installClaudeTheme();

    TVTermApp app(dirs);
    app.run();
    app.shutDown();
}

TVTermApp::TVTermApp(const std::vector<std::string> &dirs) :
    TProgInit( &TVTermApp::initStatusLine,
               &TVTermApp::initMenuBar,
               &TVTermApp::initDeskTop
             )
{
    disableCommands(tileCmds);
    for (ushort cmd : TerminalWindow::appConsts.focusedCmds())
        disableCommand(cmd);
    disableCommand(cmPaste);

    // Make room for the session counter at the right of the menu bar.
    enum { indicatorWidth = 36 };
    if (menuBar && size.x > indicatorWidth + 30)
    {
        menuBar->growTo(size.x - indicatorWidth, 1);
        indicator = new SessionIndicator(TRect(size.x - indicatorWidth, 0, size.x, 1));
        insert(indicator);
    }

    lastDir = currentDir();
    restoring = true;
    if (dirs.empty() && !restoreDesktop())
    {
        SessionSpec spec;
        newSession(spec);
    }
    for (const auto &d : dirs)
    {
        SessionSpec spec;
        spec.dir = expandHome(d);
        newSession(spec);
    }
    restoring = false;
    if (dirs.size() > 1)
        arrange(Layout::TileCols);

    // Views first drawn while the base class was being built used its
    // palette; draw everything again with the exact VGA one.
    redraw();
}

// A menu bar that refreshes File > Resume just before it opens.
class TVTermMenuBar : public TMenuBar
{
public:

    using TMenuBar::TMenuBar;

    TMenu *getMenu() const { return menu; }

    void handleEvent(TEvent &ev) override
    {
        bool opening =
               ev.what == evMouseDown
            || (ev.what == evKeyDown && ( ev.keyDown.keyCode == kbAltF ||
                                          ev.keyDown.keyCode == kbAltK ||
                                          ev.keyDown.keyCode == kbF10 ))
            || (ev.what == evCommand && ev.message.command == cmMenu);
        if (opening)
            if (auto *app = dynamic_cast<TVTermApp *>(TProgram::application))
            {
                app->refreshResumeMenu();
                app->refreshWorkspaceMenu();
            }
        TMenuBar::handleEvent(ev);
    }
};

static const char resumeMenuName[] = "~R~esume";
static const char workspaceMenuName[] = "Wor~k~space";

TMenuBar *TVTermApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    return new TVTermMenuBar(r,
        *new TSubMenu("~F~ile", kbAltF) +
            *new TMenuItem("~N~ew Claude session...", cmNewClaude, kbF3, hcNoContext, "F3") +
            *new TMenuItem(resumeMenuName, kbNoKey,
                new TMenu(*new TMenuItem("~M~ore sessions...", cmResumeMore, kbNoKey))) +
            *new TMenuItem("New ~s~hell", cmNewShell, kbNoKey) +
            newLine() +
            slashItem("/export") +
            newLine() +
            *new TMenuItem("~C~hange dir...", cmChangeDir, kbNoKey) +
            *new TMenuItem("~D~OS shell", cmDosShell, kbNoKey) +
            *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X") +
        *new TSubMenu("~E~dit", kbAltE) +
            slashItem("/rewind") +
            newLine() +
            slashItem("/copy") +
            *new TMenuItem("~S~elect text", cmStartSelection, kbNoKey) +
            *new TMenuItem("~P~aste", cmPaste, kbNoKey) +
        *new TSubMenu("~S~ession", kbAltS) +
            *new TMenuItem("Session ~l~ist...", cmSessionList, kbAlt0, hcNoContext, "Alt-0") +
            *new TMenuItem("Next ~n~eeding you", cmNextWaiting, kbF8, hcNoContext, "F8") +
            *new TMenuItem("~R~ename...", cmRenameSession, kbNoKey) +
            newLine() +
            slashItem("/clear") +
            slashItem("/compact") +
            slashItem("/context") +
            slashItem("/code-review") +
            newLine() +
            *new TMenuItem("New Claude ~s~ession...", cmNewClaude, kbF3, hcNoContext, "F3") +
        *new TSubMenu("~O~ptions", kbAltO) +
            slashItem("/model") +
            slashItem("/permissions") +
            newLine() +
            slashItem("/config") +
        *new TSubMenu("~I~nfo", kbAltI) +
            slashItem("/usage") +
            slashItem("/stats") +
        *new TSubMenu("~W~indow", kbAltW) +
            *new TMenuItem("~T~ile", cmTileCols, kbNoKey) +
            *new TMenuItem("Tile ~r~ows", cmTileRows, kbNoKey) +
            *new TMenuItem("C~a~scade", cmCascade, kbNoKey) +
            newLine() +
            *new TMenuItem("~S~ize/Move", cmResize, kbCtrlF5, hcNoContext, "Ctrl-F5") +
            *new TMenuItem("~Z~oom", cmZoom, kbF5, hcNoContext, "F5") +
            *new TMenuItem("~N~ext", cmNext, kbF6, hcNoContext, "F6") +
            *new TMenuItem("~P~revious", cmPrev, kbShiftF6, hcNoContext, "Shift-F6") +
            *new TMenuItem("~C~lose", cmClose, kbAltF3, hcNoContext, "Alt-F3") +
            newLine() +
            *new TMenuItem("~G~rab input", cmGrabInput, kbNoKey) +
        *new TSubMenu(workspaceMenuName, kbAltK) +
            *new TMenuItem("~N~ew workspace...", cmWsNew, kbNoKey) +
            *new TMenuItem("Next workspace", cmWsNext, kbF7, hcNoContext, "F7") +
            *new TMenuItem("Previous workspace", cmWsPrev, kbShiftF7, hcNoContext, "Shift-F7") +
        *new TSubMenu("~H~elp", kbAltH) +
            *new TMenuItem("~A~bout...", cmAbout, kbNoKey) +
            slashItem("/status")
        );
}

TStatusLine *TVTermApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    TStatusLine *statusLine = new TStatusLine(r,
        *new TStatusDef(hcDragging, hcDragging) +
            *new TStatusItem("~\x18\x19\x1A\x1B~ Move", kbNoKey, 0) +
            *new TStatusItem("~Shift-\x18\x19\x1A\x1B~ Resize", kbNoKey, 0) +
            *new TStatusItem("~Enter~ Done", kbNoKey, 0) +
            *new TStatusItem("~Esc~ Cancel", kbNoKey, 0) +
        *new TStatusDef(hcInputGrabbed, hcInputGrabbed) +
            *new TStatusItem("~Alt-End~ Release Input", kbAltEnd, cmReleaseInput) +
        *new TStatusDef(hcSelecting, hcSelecting) +
            *new TStatusItem("~Click & Drag~ Select Text", kbNoKey, 0) +
            *new TStatusItem("~Ctrl-C~ Copy", kbCtrlC, cmCopySelection) +
            *new TStatusItem("~Esc~ Cancel", kbEsc, cmCancelSelection) +
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
            *new TStatusItem("~F3~ New", kbF3, cmNewClaude) +
            *new TStatusItem("~Alt-0~ Sessions", kbAlt0, cmSessionList) +
            *new TStatusItem("~F6~ Next", kbF6, cmNext) +
            *new TStatusItem("~F8~ Needs you", kbF8, cmNextWaiting) +
            *new TStatusItem("~F5~ Zoom", kbF5, cmZoom) +
            *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit)
            );
    statusLine->growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
    return statusLine;
}

TDeskTop *TVTermApp::initDeskTop(TRect r)
{
    r.a.y += 1;
    r.b.y -= 1;
    return new TVTermDesk(r);
}

void TVTermApp::handleEvent(TEvent &event)
{
    if (event.what == evCommand)
    {
        Layout how = Layout::None;
        switch (event.message.command)
        {
            case cmCascade: how = Layout::Cascade; break;
            case cmTile:
            case cmTileCols: how = Layout::TileCols; break;
            case cmTileRows: how = Layout::TileRows; break;
        }
        if (how != Layout::None)
        {
            arrange(how);
            clearEvent(event);
            return;
        }
    }
    TApplication::handleEvent(event);
    bool handled = true;
    switch (event.what)
    {
        case evCommand:
            switch (event.message.command)
            {
                case cmNewClaude: newClaudeDialog(); break;
                case cmNewShell:
                {
                    SessionSpec spec;
                    spec.isClaude = false;
                    newSession(spec);
                    break;
                }
                case cmSessionList: sessionList(); break;
                case cmNextWaiting: nextWaiting(); break;
                case cmChangeDir: changeDir(); break;
                case cmAbout: about(); break;
                case cmRenameSession: renameSession(); break;
                case cmResumeMore: resumeDialog(); break;
                case cmWsNew: newWorkspaceDialog(); break;
                case cmWsRename: renameWorkspace(); break;
                case cmWsClose: closeWorkspace(); break;
                case cmWsNext: switchWorkspace((currentWs + 1) % (int) workspaces.size()); break;
                case cmWsPrev:
                    switchWorkspace((currentWs + (int) workspaces.size() - 1) % (int) workspaces.size());
                    break;
                default:
                    if ( event.message.command >= cmResume0 &&
                         event.message.command < cmResume0 + 9 )
                        resume(event.message.command - cmResume0);
                    else if ( event.message.command >= cmWs0 &&
                              event.message.command < cmWs0 + 9 )
                        switchWorkspace(event.message.command - cmWs0);
                    else if ( event.message.command >= cmClaude0 &&
                              event.message.command < cmClaude0 + std::size(claudeCommands) )
                        sendToClaude(claudeCommands[event.message.command - cmClaude0].command);
                    else
                        handled = false;
                    break;
            }
            break;
        default:
            handled = false;
            break;
    }
    if (handled)
        clearEvent(event);
}

size_t TVTermApp::getOpenTermCount()
{
    size_t count = 0;
    message(this, evBroadcast, cmGetOpenTerms, &count);
    return count;
}

std::vector<TerminalWindow *> TVTermApp::sessionWindows()
{
    std::vector<TerminalWindow *> list;
    if (!deskTop)
        return list;
    auto collect = [] (TView *p, void *arg) {
        if (auto *w = dynamic_cast<TerminalWindow *>(p))
            ((std::vector<TerminalWindow *> *) arg)->push_back(w);
    };
    deskTop->forEach(collect, &list);
    // forEach walks from the back of the Z order; list the front window first.
    std::reverse(list.begin(), list.end());
    return list;
}

Boolean TVTermApp::valid(ushort command)
{
    if (command == cmQuit)
    {
        if (size_t count = getOpenTermCount())
        {
            auto *format = (count == 1)
                ? "There is %zu open session window.\nDo you want to quit anyway?"
                : "There are %zu open session windows.\nDo you want to quit anyway?";
            bool quit = messageBox(
                mfConfirmation | mfYesButton | mfNoButton,
                format, count
            ) == cmYes;
            if (quit)
                saveDesktop();
            return quit;
        }
        saveDesktop();
        return True;
    }
    return TApplication::valid(command);
}

void TVTermApp::idle()
{
    TApplication::idle();
    {
        // Enable or disable the cmTile and cmCascade commands.
        auto isTileable =
            [] (TView *p, void *) -> Boolean { return p->options & ofTileable; };
        if (deskTop->firstThat(isTileable, nullptr))
            enableCommands(tileCmds);
        else
            disableCommands(tileCmds);
    }
    message(this, evBroadcast, cmCheckTerminalUpdates, nullptr);

    long long now = nowMs();
    if (now - lastPollMs >= 400)
    {
        lastPollMs = now;
        pollStatuses();
    }
    checkCascade();
    // The Enter of a command typed by the Claude menu goes separately, so
    // Claude does not take the whole command for a paste.
    if (pendingEnterSession && now >= pendingEnterAt)
    {
        for (auto *w : sessionWindows())
            if (w->sessionId == pendingEnterSession)
            {
                TEvent ev {};
                ev.what = evKeyDown;
                ev.keyDown.keyCode = kbEnter;
                ev.keyDown.charScan.charCode = '\r';
                w->handleEvent(ev);
            }
        pendingEnterSession = 0;
    }
    if (now - lastSaveMs >= 5000)
    {
        lastSaveMs = now;
        saveDesktop();
    }
}

void TVTermApp::shutDown()
{
    cleanupStatusDir();
    TApplication::shutDown();
}

void TVTermApp::pollStatuses()
{
    int working = 0, waiting = 0, done = 0;
    for (auto *w : sessionWindows())
    {
        w->refreshStatus();
        switch (w->status)
        {
            case SessionStatus::Working: ++working; break;
            case SessionStatus::Waiting: ++waiting; break;
            case SessionStatus::Done:    ++done; break;
            default: break;
        }
    }
    if ( indicator && ( indicator->working != working ||
                        indicator->waiting != waiting ||
                        indicator->done != done ||
                        indicator->workspace != workspaceName() ||
                        indicator->workspaceCount != (int) workspaces.size() ) )
    {
        indicator->working = working;
        indicator->waiting = waiting;
        indicator->done = done;
        indicator->workspace = workspaceName();
        indicator->workspaceCount = (int) workspaces.size();
        indicator->drawView();
    }
}

short TVTermApp::freeWindowNumber()
{
    bool used[10] = {};
    for (auto *w : sessionWindows())
        if (w->workspace == currentWs && w->number >= 1 && w->number <= 9)
            used[w->number] = true;
    for (short n = 1; n <= 9; ++n)
        if (!used[n])
            return n;
    return wnNoNumber;
}

static void onTermError(const char *reason)
{
    messageBox(mfError | mfOKButton, "Cannot create terminal: %s.", reason);
};

TerminalWindow *TVTermApp::newSession(const SessionSpec &aSpec)
{
    using namespace tvterm;
    SessionSpec spec = aSpec;
    spec.id = nextSessionId++;
    if (!spec.dir.empty())
    {
        struct stat st;
        if (stat(spec.dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            messageBox(mfError | mfOKButton, "No such folder:\n%s", spec.dir.c_str());
            return nullptr;
        }
    }

    TRect r = deskTop->getExtent();
    SessionEmulatorFactory factory(spec);
    auto *termCtrl = TerminalController::create( TerminalWindow::viewSize(r),
                                                 factory, onTermError );
    if (!termCtrl)
        return nullptr;
    auto *w = new TerminalWindow(r, *termCtrl);
    w->sessionId = spec.id;
    w->isClaude = spec.isClaude;
    w->dir = spec.dir.empty() ? currentDir() : spec.dir;
    w->name = baseName(w->dir);
    w->workspace = currentWs;
    w->number = freeWindowNumber();
    insertWindow(w);
    if (!restoring && layout != Layout::None)
        arrange(layout);
    return w;
}

void TVTermApp::newClaudeDialog()
{
    TDialog *d = new TDialog(TRect(0, 0, 62, 11), "New Claude Session");
    d->options |= ofCentered;

    TInputLine *folder = new TInputLine(TRect(3, 3, 59, 4), PATH_MAX);
    d->insert(folder);
    d->insert(new TLabel(TRect(2, 2, 20, 3), "~F~older:", folder));
    TCheckBoxes *opts = new TCheckBoxes(TRect(3, 5, 59, 6),
        new TSItem("~C~ontinue last conversation here", nullptr));
    d->insert(opts);
    d->insert(new TButton(TRect(37, 8, 47, 10), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(49, 8, 59, 10), "Cancel", cmCancel, bfNormal));
    d->selectNext(False);

    char buf[PATH_MAX] = {};
    strnzcpy(buf, lastDir.c_str(), sizeof(buf));
    folder->setData(buf);

    TView *p = validView(d);
    if (!p)
        return;
    ushort result = deskTop->execView(p);
    std::string dir = folder->data;
    bool cont = opts->mark(0);
    TObject::destroy(p);

    if (result != cmOK)
        return;
    while (!dir.empty() && dir.back() == ' ')
        dir.pop_back();
    SessionSpec spec;
    spec.dir = expandHome(dir);
    spec.continueLast = cont;
    if (!spec.dir.empty())
        lastDir = spec.dir;
    newSession(spec);
}

void TVTermApp::sessionList()
{
    auto windows = sessionWindows();
    if (windows.empty())
    {
        messageBox(mfInformation | mfOKButton, "No sessions are open.");
        return;
    }

    TDialog *d = new TDialog(TRect(0, 0, 74, 18), "Session List");
    d->options |= ofCentered;
    TScrollBar *sb = new TScrollBar(TRect(70, 2, 71, 14));
    d->insert(sb);
    auto *list = new SessionListViewer(TRect(3, 2, 70, 14), sb);
    for (auto *w : windows)
    {
        char num[8];
        if (w->number >= 1 && w->number <= 9)
            snprintf(num, sizeof(num), "%d", (int) w->number);
        else
            strcpy(num, " ");
        std::string row = std::string(num) + "  " + (w->isClaude ? statusGlyph(w->status) : " ")
                        + "  " + clipText(w->displayName(), 26);
        if (row.size() < 34)
            row.resize(34, ' ');
        std::string state = w->isClaude ? statusName(w->status) : "shell";
        state.resize(12, ' ');
        row += state + clipText(workspaces[w->workspace], 18);
        list->items.push_back(row);
    }
    list->setRange(list->items.size());
    d->insert(list);
    d->insert(new TButton(TRect(47, 15, 59, 17), "~G~o to", cmOK, bfDefault));
    d->insert(new TButton(TRect(60, 15, 71, 17), "Cancel", cmCancel, bfNormal));
    list->select();

    TView *p = validView(d);
    if (!p)
        return;
    ushort result = deskTop->execView(p);
    short chosen = list->focused;
    TObject::destroy(p);

    if (result == cmOK && chosen >= 0 && (size_t) chosen < windows.size())
    {
        switchWorkspace(windows[chosen]->workspace);
        windows[chosen]->select();
    }
}

void TVTermApp::nextWaiting()
{
    auto windows = sessionWindows();
    // The front window is first; look behind it so F8 cycles.
    for (SessionStatus want : {SessionStatus::Waiting, SessionStatus::Done})
        for (size_t i = windows.size(); i-- > 1;)
            if (windows[i]->status == want)
            {
                switchWorkspace(windows[i]->workspace);
                windows[i]->select();
                return;
            }
}

void TVTermApp::changeDir()
{
    execDialog(new TChDirDialog(cdNormal, 0));
    lastDir = currentDir();
}

void TVTermApp::about()
{
    messageBox(mfInformation | mfOKButton,
        "\003Turbo Vision Claude\n\n"
        "\003Claude Code sessions in Turbo Vision windows.\n"
        "\003Built on tvterm by magiblot.");
}

// ---------------------------------------------------------------------------
// Resume: past Claude Code sessions, as `claude --resume` lists them.

static std::string clipText(const std::string &s, size_t width)
{
    // Clip by display columns, keeping UTF-8 sequences whole.
    std::string out;
    size_t cols = 0;
    for (size_t i = 0; i < s.size() && cols < width; ++cols)
    {
        size_t len = 1;
        unsigned char c = s[i];
        if (c >= 0xF0) len = 4; else if (c >= 0xE0) len = 3; else if (c >= 0xC0) len = 2;
        out.append(s, i, len);
        i += len;
    }
    if (cols == width && out.size() < s.size())
    {
        // Leave room for an ellipsis.
        while (!out.empty() && (out.back() & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
        out += "\xE2\x80\xA6";
    }
    return out;
}

static std::string withoutTildes(std::string s)
{
    // '~' marks a hotkey in Turbo Vision menu text.
    std::replace(s.begin(), s.end(), '~', '-');
    return s;
}

void TVTermApp::refreshResumeMenu()
{
    auto *bar = dynamic_cast<TVTermMenuBar *>(menuBar);
    if (!bar || !bar->getMenu() || !bar->getMenu()->items)
        return;
    TMenuItem *file = bar->getMenu()->items;
    if (!file->subMenu)
        return;
    TMenuItem *resumeItem = nullptr;
    for (TMenuItem *i = file->subMenu->items; i; i = i->next)
        if (i->name && strcmp(i->name, resumeMenuName) == 0)
            resumeItem = i;
    if (!resumeItem)
        return;

    resumeEntries = listPastSessions(9);

    TMenuItem *head = nullptr, **tail = &head;
    for (size_t i = 0; i < resumeEntries.size(); ++i)
    {
        const auto &s = resumeEntries[i];
        std::string text = "~" + std::to_string(i + 1) + "~ " +
            withoutTildes(clipText(s.title, 34)) + "  (" + withoutTildes(clipText(baseName(s.cwd), 16)) + ")";
        *tail = new TMenuItem(text.c_str(), (ushort) (cmResume0 + i), kbNoKey,
                              hcNoContext, ageText(s.modified).c_str());
        tail = &(*tail)->next;
    }
    if (head)
    {
        *tail = &newLine();
        tail = &(*tail)->next;
    }
    *tail = new TMenuItem("~M~ore sessions...", cmResumeMore, kbNoKey);

    delete resumeItem->subMenu;
    resumeItem->subMenu = new TMenu(*head);
}

void TVTermApp::resume(size_t index)
{
    if (index >= resumeEntries.size())
        return;
    const PastSession &s = resumeEntries[index];
    SessionSpec spec;
    spec.dir = s.cwd;
    spec.resumeId = s.id;
    newSession(spec);
}

void TVTermApp::resumeDialog()
{
    auto all = listPastSessions(200);
    if (all.empty())
    {
        messageBox(mfInformation | mfOKButton, "No past Claude sessions found.");
        return;
    }

    TDialog *d = new TDialog(TRect(0, 0, 78, 20), "Resume Session");
    d->options |= ofCentered;
    TScrollBar *sb = new TScrollBar(TRect(74, 3, 75, 16));
    d->insert(sb);
    auto *list = new SessionListViewer(TRect(3, 3, 74, 16), sb);
    for (const auto &s : all)
    {
        std::string age = ageText(s.modified);
        age.resize(8, ' ');
        std::string folder = clipText(baseName(s.cwd), 16);
        list->items.push_back(age + folder + std::string(17 - std::min<size_t>(16, cstrlen(folder.c_str())), ' ')
                              + clipText(s.title, 44));
    }
    list->setRange(list->items.size());
    d->insert(list);
    d->insert(new TStaticText(TRect(3, 2, 74, 3), "When    Folder           Title"));
    d->insert(new TButton(TRect(50, 17, 62, 19), "~R~esume", cmOK, bfDefault));
    d->insert(new TButton(TRect(63, 17, 75, 19), "Cancel", cmCancel, bfNormal));
    list->select();

    TView *p = validView(d);
    if (!p)
        return;
    ushort result = deskTop->execView(p);
    short chosen = list->focused;
    TObject::destroy(p);

    if (result == cmOK && chosen >= 0 && (size_t) chosen < all.size())
    {
        SessionSpec spec;
        spec.dir = all[chosen].cwd;
        spec.resumeId = all[chosen].id;
        newSession(spec);
    }
}

// ---------------------------------------------------------------------------
// The desktop file, as Turbo Pascal kept TURBO.DSK.
//
// One line per window, front window first, tab-separated:
//   claude|shell  ax ay bx by  number  folder  conversation-id

static std::string desktopPath()
{
    std::string dir;
    if (const char *xdg = getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        dir = xdg;
    else if (const char *home = getenv("HOME"))
        dir = std::string(home) + "/.config";
    else
        return {};
    dir += "/tvclaude";
    mkdir(dir.c_str(), 0700);
    return dir + "/desktop";
}

void TVTermApp::saveDesktop()
{
    std::string out = "# tvclaude desktop: kind\tbounds\tnumber\tfolder\tconversation\tworkspace\tname\n";
    out += "workspaces";
    for (auto &name : workspaces)
        out += "\t" + name;
    out += "\ncurrent\t" + std::to_string(currentWs) + "\n";
    static const char *layoutNames[] = {"none", "cascade", "tile", "tilerows"};
    out += std::string("layout\t") + layoutNames[(int) layout] + "\n";
    for (auto *w : sessionWindows())
    {
        if (w->isEnded())
            continue;
        TRect r = w->getBounds();
        std::string conv = w->isClaude ? readConversationId(w->sessionId) : "";
        char bounds[64];
        snprintf(bounds, sizeof(bounds), "%d %d %d %d", r.a.x, r.a.y, r.b.x, r.b.y);
        out += std::string(w->isClaude ? "claude" : "shell") + "\t" + bounds + "\t" +
               std::to_string((int) w->number) + "\t" + w->dir + "\t" + conv + "\t" +
               std::to_string(w->workspace) + "\t" + w->customName + "\n";
    }
    if (out == lastSavedDesktop)
        return;
    std::string path = desktopPath();
    if (path.empty())
        return;
    std::string tmp = path + ".tmp";
    if (FILE *f = fopen(tmp.c_str(), "w"))
    {
        bool ok = fputs(out.c_str(), f) >= 0;
        ok = (fclose(f) == 0) && ok;
        if (ok && rename(tmp.c_str(), path.c_str()) == 0)
            lastSavedDesktop = out;
    }
}

bool TVTermApp::restoreDesktop()
{
    std::string path = desktopPath();
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f)
        return false;
    struct Saved { bool isClaude; TRect bounds; short number; std::string dir, conv; int ws {0}; std::string name; };
    std::vector<std::string> savedWs;
    int savedCurrent = 0;
    std::vector<Saved> saved;
    char line[8192];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#')
            continue;
        std::vector<std::string> fields;
        std::string cur;
        for (char *p = line; *p && *p != '\n'; ++p)
            if (*p == '\t') { fields.push_back(cur); cur.clear(); }
            else cur += *p;
        fields.push_back(cur);
        if (fields[0] == "workspaces")
        {
            savedWs.assign(fields.begin() + 1, fields.end());
            continue;
        }
        if (fields[0] == "layout" && fields.size() > 1)
        {
            const std::string &v = fields[1];
            layout = v == "cascade" ? Layout::Cascade : v == "tile" ? Layout::TileCols :
                     v == "tilerows" ? Layout::TileRows : Layout::None;
            continue;
        }
        if (fields[0] == "current" && fields.size() > 1)
        {
            savedCurrent = atoi(fields[1].c_str());
            continue;
        }
        if (fields.size() < 5)
            continue;
        Saved s;
        s.isClaude = fields[0] == "claude";
        int ax, ay, bx, by;
        if (sscanf(fields[1].c_str(), "%d %d %d %d", &ax, &ay, &bx, &by) != 4)
            continue;
        s.bounds = TRect(ax, ay, bx, by);
        s.number = (short) atoi(fields[2].c_str());
        s.dir = fields[3];
        s.conv = fields[4];
        if (fields.size() > 5)
            s.ws = atoi(fields[5].c_str());
        if (fields.size() > 6)
            s.name = fields[6];
        saved.push_back(std::move(s));
    }
    fclose(f);
    if (saved.empty())
        return false;
    if (!savedWs.empty())
        workspaces = savedWs;
    for (auto &s : saved)
        if (s.ws < 0 || s.ws >= (int) workspaces.size())
            s.ws = 0;

    // Open back to front, so the saved front window ends up on top.
    TRect desk = deskTop->getExtent();
    for (size_t i = saved.size(); i-- > 0;)
    {
        const Saved &s = saved[i];
        SessionSpec spec;
        spec.isClaude = s.isClaude;
        spec.dir = s.dir;
        spec.resumeId = s.conv;
        currentWs = s.ws;
        TerminalWindow *w = newSession(spec);
        if (!w)
            continue;
        w->customName = s.name;
        if (s.number >= 1 && s.number <= 9)
        {
            bool taken = false;
            for (auto *o : sessionWindows())
                if (o != w && o->workspace == w->workspace && o->number == s.number)
                    taken = true;
            if (!taken)
            {
                w->number = s.number;
                if (w->frame)
                    w->frame->drawView();
            }
        }
        // Keep the saved place when it still fits the screen.
        TRect r = s.bounds;
        TPoint minSize, maxSize;
        w->sizeLimits(minSize, maxSize);
        bool fits = r.a.x >= -1 && r.a.y >= 0 && r.b.x <= desk.b.x + 1 &&
                    r.b.y <= desk.b.y + 1 && r.b.x - r.a.x >= minSize.x &&
                    r.b.y - r.a.y >= minSize.y;
        if (fits)
            w->locate(r);
    }
    // Show only the workspace that was on screen.
    int show = (savedCurrent >= 0 && savedCurrent < (int) workspaces.size()) ? savedCurrent : 0;
    currentWs = -1;
    switchWorkspace(show);
    return true;
}

// ---------------------------------------------------------------------------
// Workspaces.

void TVTermApp::switchWorkspace(int index)
{
    if (index < 0 || index >= (int) workspaces.size() || index == currentWs)
        return;
    currentWs = index;
    cascaded = false;
    TerminalWindow *front = nullptr;
    for (auto *w : sessionWindows())   // Front window first.
    {
        bool mine = w->workspace == index;
        if (mine && !(w->state & sfVisible))
            w->show();
        else if (!mine && (w->state & sfVisible))
            w->hide();
        if (mine && !front)
            front = w;
    }
    if (front)
        front->select();
    pollStatuses();
}

void TVTermApp::refreshWorkspaceMenu()
{
    auto *bar = dynamic_cast<TVTermMenuBar *>(menuBar);
    if (!bar || !bar->getMenu())
        return;
    TMenuItem *wsItem = nullptr;
    for (TMenuItem *i = bar->getMenu()->items; i; i = i->next)
        if (i->name && strcmp(i->name, workspaceMenuName) == 0)
            wsItem = i;
    if (!wsItem)
        return;

    std::vector<int> counts(workspaces.size(), 0);
    for (auto *w : sessionWindows())
        if (w->workspace >= 0 && w->workspace < (int) counts.size())
            ++counts[w->workspace];

    TMenuItem *head = new TMenuItem("~N~ew workspace...", cmWsNew, kbNoKey);
    TMenuItem **tail = &head->next;
    auto add = [&] (TMenuItem *item) { *tail = item; tail = &item->next; };
    add(new TMenuItem("~R~ename...", cmWsRename, kbNoKey));
    add(new TMenuItem("~C~lose workspace", cmWsClose, kbNoKey));
    add(&newLine());
    add(new TMenuItem("Next workspace", cmWsNext, kbF7, hcNoContext, "F7"));
    add(new TMenuItem("Previous workspace", cmWsPrev, kbShiftF7, hcNoContext, "Shift-F7"));
    add(&newLine());
    for (size_t i = 0; i < workspaces.size() && i < 9; ++i)
    {
        std::string text = "~" + std::to_string(i + 1) + "~ " +
            withoutTildes(clipText(workspaces[i], 28)) +
            ((int) i == currentWs ? "  \xE2\x97\x84" : "");  // ◄
        std::string count = std::to_string(counts[i]) + (counts[i] == 1 ? " window" : " windows");
        add(new TMenuItem(text.c_str(), (ushort) (cmWs0 + i), kbNoKey, hcNoContext, count.c_str()));
    }
    delete wsItem->subMenu;
    wsItem->subMenu = new TMenu(*head);
}

void TVTermApp::newWorkspaceDialog()
{
    TDialog *d = new TDialog(TRect(0, 0, 62, 12), "New Workspace");
    d->options |= ofCentered;
    TInputLine *name = new TInputLine(TRect(3, 3, 59, 4), 60);
    d->insert(name);
    d->insert(new TLabel(TRect(2, 2, 20, 3), "~N~ame:", name));
    TInputLine *folder = new TInputLine(TRect(3, 6, 59, 7), PATH_MAX);
    d->insert(folder);
    d->insert(new TLabel(TRect(2, 5, 50, 6), "~F~older for a Claude session (optional):", folder));
    d->insert(new TButton(TRect(37, 9, 47, 11), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(49, 9, 59, 11), "Cancel", cmCancel, bfNormal));
    d->selectNext(False);

    char buf[PATH_MAX] = {};
    strnzcpy(buf, lastDir.c_str(), sizeof(buf));
    folder->setData(buf);

    TView *p = validView(d);
    if (!p)
        return;
    ushort result = deskTop->execView(p);
    std::string wsName = name->data;
    std::string dir = folder->data;
    TObject::destroy(p);
    if (result != cmOK)
        return;

    auto trim = [] (std::string &s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    };
    trim(wsName);
    trim(dir);
    dir = expandHome(dir);
    std::replace(wsName.begin(), wsName.end(), '\t', ' ');
    if (wsName.empty())
        wsName = dir.empty() ? "Workspace " + std::to_string(workspaces.size() + 1)
                             : baseName(dir);
    workspaces.push_back(wsName);
    switchWorkspace((int) workspaces.size() - 1);
    if (!dir.empty())
    {
        lastDir = dir;
        SessionSpec spec;
        spec.dir = dir;
        newSession(spec);
    }
}

void TVTermApp::renameSession()
{
    auto *w = dynamic_cast<TerminalWindow *>(deskTop->current);
    if (!w)
    {
        messageBox(mfInformation | mfOKButton, "No window to rename.");
        return;
    }
    char buf[61] = {};
    strnzcpy(buf, w->displayName().c_str(), sizeof(buf));
    TDialog *d = new TDialog(TRect(0, 0, 60, 9), "Rename Session");
    d->options |= ofCentered;
    TInputLine *in = new TInputLine(TRect(3, 3, 57, 4), 60);
    d->insert(in);
    d->insert(new TLabel(TRect(2, 2, 20, 3), "~N~ame:", in));
    d->insert(new TStaticText(TRect(3, 4, 57, 5), "Leave empty to follow Claude's folder."));
    d->insert(new TButton(TRect(35, 6, 45, 8), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(47, 6, 57, 8), "Cancel", cmCancel, bfNormal));
    d->selectNext(False);
    d->setData(buf);
    if (execView(d) == cmOK)
    {
        d->getData(buf);
        std::string name = buf;
        std::replace(name.begin(), name.end(), '\t', ' ');
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        // Typing the automatic name back in means: keep following it.
        w->setCustomName(name == w->name ? std::string() : name);
    }
    TObject::destroy(d);
}

void TVTermApp::sendToClaude(const char *command)
{
    auto *w = dynamic_cast<TerminalWindow *>(deskTop->current);
    if (!w || !w->isClaude || w->isEnded())
    {
        messageBox(mfInformation | mfOKButton,
            "Select a Claude window first; %s goes to the window in front.", command);
        return;
    }
    for (const char *p = command; *p; ++p)
    {
        TEvent ev {};
        ev.what = evKeyDown;
        ev.keyDown.keyCode = (uchar) *p;
        ev.keyDown.text[0] = *p;
        ev.keyDown.textLength = 1;
        w->handleEvent(ev);
    }
    pendingEnterSession = w->sessionId;
    pendingEnterAt = nowMs() + 150;
}

void TVTermApp::renameWorkspace()
{
    char buf[61] = {};
    strnzcpy(buf, workspaces[currentWs].c_str(), sizeof(buf));
    if (inputBox("Rename Workspace", "~N~ame:", buf, 60) != cmOK)
        return;
    std::string name = buf;
    std::replace(name.begin(), name.end(), '\t', ' ');
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    if (!name.empty())
        workspaces[currentWs] = name;
    pollStatuses();
}

void TVTermApp::closeWorkspace()
{
    if (workspaces.size() <= 1)
    {
        messageBox(mfInformation | mfOKButton, "This is the only workspace.");
        return;
    }
    for (auto *w : sessionWindows())
        if (w->workspace == currentWs && !w->isEnded())
        {
            messageBox(mfInformation | mfOKButton,
                "Close the windows in \"%s\" first.", workspaces[currentWs].c_str());
            return;
        }
    int gone = currentWs;
    // Ended windows go with it.
    for (auto *w : sessionWindows())
        if (w->workspace == gone)
            TObject::destroy(w);
    for (auto *w : sessionWindows())
        if (w->workspace > gone)
            --w->workspace;
    workspaces.erase(workspaces.begin() + gone);
    currentWs = -1;
    switchWorkspace(gone > 0 ? gone - 1 : 0);
}

// ---------------------------------------------------------------------------
// Cascade that stays cascaded.

void TVTermApp::cascadeWindows()
{
    deskTop->cascade(getTileRect());
    cascadeBounds.clear();
    for (auto *w : sessionWindows())
        if (w->state & sfVisible)
            cascadeBounds.push_back({w, w->getBounds()});
    cascadeFront = deskTop->current;
    cascaded = !cascadeBounds.empty();
}

void TVTermApp::arrange(Layout how)
{
    layout = how;
    switch (how)
    {
        case Layout::Cascade: cascadeWindows(); break;
        case Layout::TileCols: getDeskTop()->tileVertical(getTileRect()); break;
        case Layout::TileRows: getDeskTop()->tileHorizontal(getTileRect()); break;
        case Layout::None: break;
    }
}

void TVTermApp::checkCascade()
{
    if (!cascaded)
        return;
    TView *cur = deskTop->current;
    if (cur && (cur->state & sfDragging))
        return;   // Wait for the drag to end.

    auto windows = sessionWindows();
    size_t visible = 0;
    for (auto *w : windows)
        if (w->state & sfVisible)
            ++visible;
    // A window moved or resized by hand ends cascade mode.
    for (auto &[view, bounds] : cascadeBounds)
        for (auto *w : windows)
            if (w == view && (w->state & sfVisible) && w->getBounds() != bounds)
            {
                cascaded = false;
                return;
            }
    // A window brought forward, opened or closed: cascade again.
    if (cur != cascadeFront || visible != cascadeBounds.size())
        cascadeWindows();
}
