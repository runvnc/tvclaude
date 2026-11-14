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
#define Uses_MsgBox
#include <tvision/tv.h>

#include "app.h"
#include "cmds.h"
#include "desk.h"
#include "wnd.h"
#include "apputil.h"
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

// ---------------------------------------------------------------------------
// The session counter at the right end of the menu bar.

class SessionIndicator : public TView
{
public:

    int working {0}, waiting {0}, done {0};

    SessionIndicator(const TRect &r) noexcept : TView(r)
    {
        growMode = gfGrowLoX | gfGrowHiX;
    }

    void draw() override
    {
        // The menu bar's colors: black on gray, the alarm in red.
        const TColorAttr normal = 0x70, alarm = 0x74, ok = 0x72;
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

        int width = 0;
        for (auto &p : pieces)
            width += cstrlen(p.text.c_str()) + 2;
        int x = size.x - width;
        if (x < 0)
            x = 0;
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
    if (dirs.empty())
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
    if (dirs.size() > 1)
        getDeskTop()->tileVertical(getTileRect());
}

TMenuBar *TVTermApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    return new TMenuBar(r,
        *new TSubMenu("~F~ile", kbAltF) +
            *new TMenuItem("~N~ew Claude session...", cmNewClaude, kbF3, hcNoContext, "F3") +
            *new TMenuItem("New ~s~hell", cmNewShell, kbNoKey) +
            newLine() +
            *new TMenuItem("~C~hange dir...", cmChangeDir, kbNoKey) +
            *new TMenuItem("~D~OS shell", cmDosShell, kbNoKey) +
            *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X") +
        *new TSubMenu("~E~dit", kbAltE) +
            *new TMenuItem("~S~elect text", cmStartSelection, kbNoKey) +
            *new TMenuItem("~P~aste", cmPaste, kbNoKey) +
        *new TSubMenu("~S~ession", kbAltS) +
            *new TMenuItem("Session ~l~ist...", cmSessionList, kbAlt0, hcNoContext, "Alt-0") +
            *new TMenuItem("Next ~n~eeding you", cmNextWaiting, kbF8, hcNoContext, "F8") +
            newLine() +
            *new TMenuItem("~N~ew Claude session...", cmNewClaude, kbF3, hcNoContext, "F3") +
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
        *new TSubMenu("~H~elp", kbAltH) +
            *new TMenuItem("~A~bout...", cmAbout, kbNoKey)
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
                case cmTileCols: getDeskTop()->tileVertical(getTileRect()); break;
                case cmTileRows: getDeskTop()->tileHorizontal(getTileRect()); break;
                default:
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
            return messageBox(
                mfConfirmation | mfYesButton | mfNoButton,
                format, count
            ) == cmYes;
        }
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
                        indicator->done != done ) )
    {
        indicator->working = working;
        indicator->waiting = waiting;
        indicator->done = done;
        indicator->drawView();
    }
}

short TVTermApp::freeWindowNumber()
{
    bool used[10] = {};
    for (auto *w : sessionWindows())
        if (w->number >= 1 && w->number <= 9)
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

void TVTermApp::newSession(const SessionSpec &aSpec)
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
            return;
        }
    }

    TRect r = deskTop->getExtent();
    SessionEmulatorFactory factory(spec);
    auto *termCtrl = TerminalController::create( TerminalWindow::viewSize(r),
                                                 factory, onTermError );
    if (!termCtrl)
        return;
    auto *w = new TerminalWindow(r, *termCtrl);
    w->sessionId = spec.id;
    w->isClaude = spec.isClaude;
    w->name = baseName(spec.dir.empty() ? currentDir() : spec.dir);
    w->number = freeWindowNumber();
    insertWindow(w);
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

    TDialog *d = new TDialog(TRect(0, 0, 64, 18), "Session List");
    d->options |= ofCentered;
    TScrollBar *sb = new TScrollBar(TRect(60, 2, 61, 14));
    d->insert(sb);
    auto *list = new SessionListViewer(TRect(3, 2, 60, 14), sb);
    for (auto *w : windows)
    {
        char num[8];
        if (w->number >= 1 && w->number <= 9)
            snprintf(num, sizeof(num), "%d", (int) w->number);
        else
            strcpy(num, " ");
        std::string row = std::string(num) + "  " + (w->isClaude ? statusGlyph(w->status) : " ")
                        + "  " + w->name;
        if (row.size() < 34)
            row.resize(34, ' ');
        row += w->isClaude ? statusName(w->status) : "shell";
        list->items.push_back(row);
    }
    list->setRange(list->items.size());
    d->insert(list);
    d->insert(new TButton(TRect(37, 15, 49, 17), "~G~o to", cmOK, bfDefault));
    d->insert(new TButton(TRect(50, 15, 61, 17), "Cancel", cmCancel, bfNormal));
    list->select();

    TView *p = validView(d);
    if (!p)
        return;
    ushort result = deskTop->execView(p);
    short chosen = list->focused;
    TObject::destroy(p);

    if (result == cmOK && chosen >= 0 && (size_t) chosen < windows.size())
        windows[chosen]->select();
}

void TVTermApp::nextWaiting()
{
    auto windows = sessionWindows();
    // The front window is first; look behind it so F8 cycles.
    for (SessionStatus want : {SessionStatus::Waiting, SessionStatus::Done})
        for (size_t i = windows.size(); i-- > 1;)
            if (windows[i]->status == want)
            {
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
