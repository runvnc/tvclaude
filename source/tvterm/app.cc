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
#define Uses_TBackground
#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TFileDialog
#define Uses_TFileList
#define Uses_TFileCollection
#define Uses_TSearchRec
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
#include "theme.h"
#include <tvterm/termctrl.h>
#include <tvterm/vtermemu.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <cerrno>
#include <time.h>
#include <algorithm>
#include <set>
#include <sys/stat.h>
#include <dirent.h>

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
    {"Back~g~round tasks", "/tasks"},
    {"~M~emory...", "/memory"},
    {"~I~nit CLAUDE.md", "/init"},
    {"~A~dd folder...", "/add-dir"},
    {"~M~odel...", "/model"},                   // Options
    {"~E~ffort...", "/effort"},
    {"~F~ast mode", "/fast"},
    {"~P~ermissions...", "/permissions"},
    {"Output st~y~le...", "/output-style"},
    {"MCP ser~v~ers...", "/mcp"},
    {"~H~ooks...", "/hooks"},
    {"Pl~u~gins...", "/plugin"},
    {"S~k~ills...", "/skills"},
    {"~C~onfig...", "/config"},
    {"~U~sage", "/usage"},                      // Info
    {"S~t~ats", "/stats"},
    {"~D~octor", "/doctor"},
    {"~R~elease notes", "/release-notes"},
    {"Claude ~s~tatus", "/status"},             // Help
    {"Claude ~h~elp", "/help"},
    {"Send ~f~eedback...", "/feedback"},
};

static TMenuItem &slashItem(const char *command)
{
    for (size_t i = 0; i < std::size(claudeCommands); ++i)
        if (strcmp(claudeCommands[i].command, command) == 0)
            return *new TMenuItem(claudeCommands[i].label, ushort(cmClaude0 + i),
                                  kbNoKey);
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
// Colors. Turbo Vision's palette names the 16 text-mode colors by index,
// which each terminal paints from its own palette (often not the DOS one).
// The current theme turns them into exact RGB (theme.cc), so the desktop
// looks the same everywhere.

static TColorAttr vgaAttr(uchar bios)
{
    return themeAttr(bios);
}

TPalette &TVTermApp::getPalette() const
{
    if (appPalette != apColor)
        return TApplication::getPalette();
    static const char bios[] = cpAppColor;
    enum { n = sizeof(bios) - 1 };
    static TColorAttr attrs[n];
    static TPalette rgb(attrs, (ushort) n);
    static size_t built = (size_t) -1;
    if (built != currentThemeIndex())
    {
        built = currentThemeIndex();
        const Theme &t = currentTheme();
        for (int i = 0; i < n; ++i)
            attrs[i] = themeAttr((uchar) bios[i]);
        if (!t.vga)
        {
            auto color = [] (unsigned c) {
                return TColorRGB(c >> 16, (c >> 8) & 0xFF, c & 0xFF);
            };
            // The desktop's own color.
            attrs[0] = TColorAttr(color(t.deskFg), color(t.desk));
            // Button shadows in the gray, blue and cyan dialogs are drawn
            // in "black", which the theme turns into text color: use the
            // shadow color instead.
            for (int i : {45, 77, 109})
                attrs[i] = TColorAttr(color(t.shadow), attrs[i].getBackground());
        }
        rgb = TPalette(attrs, (ushort) n);
    }
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
            left += b.moveStr(left, ws.c_str(), vgaAttr(currentTheme().vga ? 0x71 : 0x70)) + 2;
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

static bool underDtach();

// Runs tvclaude under dtach when it is installed: the first start creates
// the socket, a later one reattaches to the running desktop. Returns only
// when that is not possible or not wanted.
static void execUnderDtach(int argc, char **argv)
{
    if (underDtach() || getenv("TVCLAUDE_NO_DTACH") || !isatty(0) || !isatty(1))
        return;
    std::string dtach;
    if (const char *path = getenv("PATH"))
    {
        std::string dirs = path;
        for (size_t at = 0; at <= dirs.size() && dtach.empty();)
        {
            size_t end = dirs.find(':', at);
            if (end == std::string::npos)
                end = dirs.size();
            std::string cand = dirs.substr(at, end - at) + "/dtach";
            if (end > at && access(cand.c_str(), X_OK) == 0)
                dtach = cand;
            at = end + 1;
        }
    }
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (dtach.empty() || n <= 0)
        return;
    self[n] = 0;
    std::string sock;
    if (const char *run = getenv("XDG_RUNTIME_DIR"); run && *run)
        sock = std::string(run) + "/tvclaude.sock";
    else
        sock = "/tmp/tvclaude-" + std::to_string((int) getuid()) + ".sock";
    // Should dtach fail to start us, do not try again from inside it.
    setenv("TVCLAUDE_NO_DTACH", "1", 1);
    std::vector<char *> args = {(char *) "dtach", (char *) "-A", (char *) sock.c_str(),
                                (char *) "-r", (char *) "winch", (char *) "-z", self};
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(nullptr);
    execv(dtach.c_str(), args.data());
    unsetenv("TVCLAUDE_NO_DTACH");
}

int main(int argc, char **argv)
{
    std::vector<std::string> dirs;
    bool noDtach = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--no-dtach")
            noDtach = true;
        else if (a == "-h" || a == "--help")
        {
            printf("usage: tvclaude [--no-dtach] [folder...]\n\n"
                   "Opens a Claude Code session per folder (default: the saved\n"
                   "desktop, or one in the current folder). With dtach installed,\n"
                   "runs under it: closing the terminal detaches, and running\n"
                   "tvclaude again reattaches. --no-dtach runs it directly.\n");
            return 0;
        }
        else
            dirs.push_back(a);
    }
    if (!noDtach)
        execUnderDtach(argc, argv);
    unsetenv("TVCLAUDE_NO_DTACH");   // Not passed on to the sessions.
    TVTermApp app(dirs);
    app.run();
    app.shutDown();
}

// True when tvclaude was started by dtach, so the terminal can detach.
static std::string readProcFile(int pid, const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/%s", pid, name);
    std::string out;
    if (FILE *f = fopen(path, "r"))
    {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
    }
    return out;
}

static bool underDtach()
{
    return readProcFile((int) getppid(), "comm").rfind("dtach", 0) == 0;
}

// The dtach socket: the argument after -a, -A, -c, -n or -N in the
// command line of the dtach master that started tvclaude.
static std::string dtachSocket(int pid)
{
    std::string cmd = readProcFile(pid, "cmdline");
    std::vector<std::string> args;
    for (size_t at = 0; at < cmd.size();)
    {
        size_t end = cmd.find('\0', at);
        if (end == std::string::npos)
            end = cmd.size();
        args.push_back(cmd.substr(at, end - at));
        at = end + 1;
    }
    for (size_t i = 1; i + 1 < args.size(); ++i)
    {
        const std::string &a = args[i];
        if (a == "-a" || a == "-A" || a == "-c" || a == "-n" || a == "-N")
            return args[i + 1];
    }
    return {};
}

static std::string configDir()
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
    return dir;
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
    if (!underDtach())
        disableCommand(cmDetach);
    loadOptions();
    // The saved theme, else the classic one: the session windows' colors
    // and the Claude Code theme are set before any session starts.
    if (!selectTheme(themeId))
        selectTheme((size_t) 0);
    themeChanged();

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

// Options > Theme: Retro and Modern submenus, one entry per theme; the
// current one is ticked.
static std::vector<TMenuItem *> themeItems;

static std::string themeItemName(size_t i)
{
    return std::string(i == currentThemeIndex() ? "\xE2\x88\x9A " : "  ") + themes[i].name;
}

static TMenu *themeMenu()
{
    themeItems.assign(themeCount, nullptr);
    TMenuItem *groups[2] = {nullptr, nullptr};
    for (int retro = 1; retro >= 0; --retro)
    {
        TMenuItem *first = nullptr, *last = nullptr;
        for (size_t i = 0; i < themeCount; ++i)
            if (themes[i].retro == (bool) retro)
            {
                auto *item = new TMenuItem(themeItemName(i).c_str(), ushort(cmTheme0 + i), kbNoKey);
                themeItems[i] = item;
                (last ? last->next : first) = item;
                last = item;
            }
        groups[retro] = first;
    }
    return new TMenu(
        *new TMenuItem("~R~etro", kbNoKey, new TMenu(*groups[1])) +
        *new TMenuItem("~M~odern", kbNoKey, new TMenu(*groups[0])));
}

static void markThemeItems()
{
    for (size_t i = 0; i < themeItems.size(); ++i)
        if (TMenuItem *item = themeItems[i])
        {
            delete[] (char *) item->name;
            item->name = newStr(themeItemName(i).c_str());
        }
}

// After a theme change: the desktop, the menu ticks, every session window's
// contents (hidden workspaces too) and the whole screen.
void TVTermApp::themeChanged()
{
    markThemeItems();
    if (deskTop && deskTop->background)
        deskTop->background->pattern = currentTheme().pattern;
    for (auto *w : sessionWindows())
        w->redrawContents();
    // Windows keep a drawing of themselves (ofBuffered); redraw those too.
    if (deskTop)
        deskTop->forEach([] (TView *p, void *) {
            if (auto *g = dynamic_cast<TGroup *>(p))
                g->redraw();
        }, nullptr);
    redraw();
}

void TVTermApp::setTheme(size_t index)
{
    if (!selectTheme(index))
        return;
    themeChanged();
    saveOptions();
}

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
            *new TMenuItem("~A~ttach file(s)...", cmAttachFile, kbNoKey) +
            slashItem("/export") +
            newLine() +
            *new TMenuItem("~C~hange dir...", cmChangeDir, kbNoKey) +
            *new TMenuItem("~D~OS shell", cmDosShell, kbNoKey) +
            newLine() +
            *new TMenuItem("De~t~ach (keep running)", cmDetach, kbNoKey) +
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
            slashItem("/tasks") +
            newLine() +
            slashItem("/memory") +
            slashItem("/init") +
            slashItem("/add-dir") +
            newLine() +
            *new TMenuItem("New Claude ~s~ession...", cmNewClaude, kbF3, hcNoContext, "F3") +
        *new TSubMenu("~O~ptions", kbAltO) +
            slashItem("/model") +
            slashItem("/effort") +
            slashItem("/fast") +
            slashItem("/permissions") +
            slashItem("/output-style") +
            newLine() +
            slashItem("/mcp") +
            slashItem("/hooks") +
            slashItem("/plugin") +
            slashItem("/skills") +
            newLine() +
            slashItem("/config") +
            newLine() +
            *new TMenuItem("~T~heme", kbNoKey, themeMenu()) +
            *new TMenuItem("On e~x~it...", cmExitOptions, kbNoKey) +
        *new TSubMenu("~I~nfo", kbAltI) +
            slashItem("/usage") +
            slashItem("/stats") +
            newLine() +
            slashItem("/doctor") +
            slashItem("/release-notes") +
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
            newLine() +
            slashItem("/status") +
            slashItem("/help") +
            slashItem("/feedback")
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

// Turns the terminal's mouse reporting, bracketed paste and key modes back on.
// They belong to the terminal, not to tvclaude: after a dtach reattach the new
// terminal has none of them, so the mouse would do nothing. dtach resizes the
// screen on every attach, so this runs on every screen change; sending them
// again to a terminal that already has them is harmless.
static void terminalModesOn()
{
    static const char seq[] =
        "\x1B[?1000h"   // Mouse reporting.
        "\x1B[?1002h"   // Mouse drag reporting.
        "\x1B[?1006h"   // SGR extended mouse reporting.
        "\x1B[?1036h"   // metaSendsEscape (XTerm).
        "\x1B[?2004h"   // Bracketed paste.
        "\x1B[>4;1m"    // modifyOtherKeys (XTerm).
        "\x1B[=5;1u"    // Kitty keyboard flags, set rather than pushed.
        ;
    if (isatty(STDOUT_FILENO))
    {
        size_t done = 0;
        while (done < sizeof(seq) - 1)
        {
            ssize_t n = write(STDOUT_FILENO, seq + done, sizeof(seq) - 1 - done);
            if (n <= 0 && errno != EINTR)
                break;
            if (n > 0)
                done += n;
        }
    }
}

// tvision handles SIGWINCH and swallows the resulting cmScreenChanged inside
// TProgram::getEvent, so we chain our own handler in front of its one to
// learn about screen changes. tvision reinstalls its handler when it resumes
// (after DOS shell), hence the check, once a second.
static volatile sig_atomic_t screenChanged = 0;
static struct sigaction nextWinch;

static void onWinch(int sig, siginfo_t *info, void *ctx)
{
    screenChanged = 1;
    if (nextWinch.sa_flags & SA_SIGINFO)
    {
        if (nextWinch.sa_sigaction)
            nextWinch.sa_sigaction(sig, info, ctx);
    }
    else if (nextWinch.sa_handler != SIG_DFL && nextWinch.sa_handler != SIG_IGN)
        nextWinch.sa_handler(sig);
}

static void watchWinch()
{
    struct sigaction cur;
    if (sigaction(SIGWINCH, nullptr, &cur) == -1)
        return;
    if ((cur.sa_flags & SA_SIGINFO) && cur.sa_sigaction == onWinch)
        return;
    struct sigaction sa {};
    sa.sa_sigaction = onWinch;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigfillset(&sa.sa_mask);
    nextWinch = cur;
    sigaction(SIGWINCH, &sa, nullptr);
}

void TVTermApp::getEvent(TEvent &event)
{
    static long long lastWatch = 0;
    long long now = nowMs();
    if (now - lastWatch >= 1000)
    {
        lastWatch = now;
        watchWinch();
    }
    TApplication::getEvent(event);
    if (screenChanged)
    {
        screenChanged = 0;
        terminalModesOn();
    }
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
                case cmDetach: detach(); break;
                case cmExitOptions: exitOptions(); break;
                case cmAttachFile: attachFile(); break;
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
                    else if ( event.message.command >= cmTheme0 &&
                              event.message.command < cmTheme0 + themeCount )
                        setTheme(event.message.command - cmTheme0);
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
    // forEach walks from the front of the Z order to the back.
    deskTop->forEach(collect, &list);
    return list;
}

Boolean TVTermApp::valid(ushort command)
{
    if (command == cmQuit)
    {
        size_t count = getOpenTermCount();
        if (count && underDtach())
        {
            ExitChoice choice = exitChoice;
            if (choice == ExitChoice::Ask)
            {
                // Keep running / Quit / Cancel, and remember the choice.
                TDialog *d = new TDialog(TRect(0, 0, 58, 13), "Exit");
                d->options |= ofCentered;
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "%zu session%s open. Keep %s running in the "
                    "background? Run the same dtach command "
                    "again to come back.", count, count == 1 ? " is" : "s are",
                    count == 1 ? "it" : "them");
                d->insert(new TStaticText(TRect(3, 2, 55, 6), msg));
                auto *remember = new TCheckBoxes(TRect(3, 7, 55, 8),
                    new TSItem("~R~emember my choice (Options > On exit)", nullptr));
                d->insert(remember);
                d->insert(new TButton(TRect(3, 10, 21, 12), "~K~eep running", cmNo, bfDefault));
                d->insert(new TButton(TRect(22, 10, 38, 12), "~Q~uit all", cmYes, bfNormal));
                d->insert(new TButton(TRect(39, 10, 54, 12), "Cancel", cmCancel, bfNormal));
                d->selectNext(False);
                d->selectNext(False);   // Focus Keep running.
                ushort result = deskTop->execView(d);
                ushort rememberIt = 0;
                remember->getData(&rememberIt);
                TObject::destroy(d);
                if (result == cmCancel)
                    return False;
                choice = result == cmNo ? ExitChoice::Detach : ExitChoice::Quit;
                if (rememberIt & 1)
                {
                    exitChoice = choice;
                    saveOptions();
                }
            }
            if (choice == ExitChoice::Detach)
            {
                detach();
                return False;
            }
            saveDesktop();
            return True;
        }
        if (count)
        {
            char msg[512];
            snprintf(msg, sizeof(msg),
                "Quitting ends %zu open session%s.\n\n"
                "To keep sessions running when you leave, start "
                "tvclaude under dtach (see the README) and detach "
                "instead of quitting.\n\nQuit anyway?",
                count, count == 1 ? "" : "s");
            TRect r(0, 0, 54, 13);
            r.move((size.x - r.b.x) / 2, (size.y - r.b.y) / 2);
            if (messageBoxRect(r, msg, mfConfirmation | mfYesButton | mfNoButton) != cmYes)
                return False;
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
    checkTile();
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

bool TVTermApp::detach()
{
    // The dtach client (the process attached to the terminal) is any dtach
    // other than our master using the same socket. SIGHUP makes it print
    // [detached] and exit; the master and the sessions carry on.
    int master = (int) getppid();
    std::string sock = dtachSocket(master);
    int ended = 0;
    if (!sock.empty())
        if (DIR *proc = opendir("/proc"))
        {
            while (dirent *e = readdir(proc))
            {
                int pid = atoi(e->d_name);
                if (pid <= 0 || pid == master)
                    continue;
                if (readProcFile(pid, "comm").rfind("dtach", 0) != 0)
                    continue;
                if (dtachSocket(pid) == sock && kill(pid, SIGHUP) == 0)
                    ++ended;
            }
            closedir(proc);
        }
    saveDesktop();
    if (!ended)
        messageBox(mfError | mfOKButton,
            "Could not find the dtach client to detach.\n"
            "Press Ctrl-\\ to detach instead.");
    return ended > 0;
}

static std::string optionsPath()
{
    std::string dir = configDir();
    return dir.empty() ? dir : dir + "/options";
}

void TVTermApp::loadOptions()
{
    std::string path = optionsPath();
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
            l.pop_back();
        if (l == "exit\tdetach") exitChoice = ExitChoice::Detach;
        else if (l == "exit\tquit") exitChoice = ExitChoice::Quit;
        else if (l == "exit\task") exitChoice = ExitChoice::Ask;
        else if (l.rfind("attachdir\t", 0) == 0) attachDir = l.substr(10);
        else if (l.rfind("theme\t", 0) == 0) themeId = l.substr(6);
    }
    fclose(f);
}

void TVTermApp::saveOptions()
{
    std::string path = optionsPath();
    if (path.empty())
        return;
    if (FILE *f = fopen(path.c_str(), "w"))
    {
        const char *names[] = {"ask", "detach", "quit"};
        fprintf(f, "# tvclaude options\nexit\t%s\n", names[(int) exitChoice]);
        if (!attachDir.empty())
            fprintf(f, "attachdir\t%s\n", attachDir.c_str());
        fprintf(f, "theme\t%s\n", currentTheme().id);
        fclose(f);
    }
}

void TVTermApp::exitOptions()
{
    TDialog *d = new TDialog(TRect(0, 0, 56, 13), "On Exit");
    d->options |= ofCentered;
    auto *choice = new TRadioButtons(TRect(3, 3, 53, 6),
        new TSItem("~A~sk each time",
        new TSItem("~K~eep sessions running (detach)",
        new TSItem("~Q~uit, ending all sessions", nullptr))));
    d->insert(choice);
    d->insert(new TLabel(TRect(2, 2, 40, 3), "With sessions open, Exit (Alt-X):", choice));
    d->insert(new TStaticText(TRect(3, 7, 53, 9),
        underDtach() ? "Applies under dtach; otherwise Exit asks to quit."
                     : "Applies under dtach. tvclaude is not under dtach now, "
                       "so Exit asks to quit."));
    d->insert(new TButton(TRect(14, 10, 26, 12), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(28, 10, 40, 12), "Cancel", cmCancel, bfNormal));
    d->selectNext(False);
    ushort value = (ushort) exitChoice;
    choice->setData(&value);
    ushort result = deskTop->execView(d);
    if (result == cmOK)
    {
        choice->getData(&value);
        exitChoice = (ExitChoice) value;
        saveOptions();
    }
    TObject::destroy(d);
}

extern const char buildNumber[], buildCommit[], buildDate[];

void TVTermApp::about()
{
    char msg[256];
    snprintf(msg, sizeof(msg),
        "\003Turbo Vision Claude\n"
        "\003Build %s (%s), %s\n\n"
        "\003Claude Code sessions in Turbo Vision windows.\n"
        "\003Built on tvterm by magiblot.",
        buildNumber, buildCommit, buildDate);
    TRect r(0, 0, 54, 11);
    r.move((size.x - r.b.x) / 2, (size.y - r.b.y) / 2);
    messageBoxRect(r, msg, mfInformation | mfOKButton);
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
    std::string dir = configDir();
    return dir.empty() ? dir : dir + "/desktop";
}

void TVTermApp::saveDesktop()
{
    std::string out = "# tvclaude desktop: kind\tbounds\tnumber\tfolder\tconversation\tworkspace\tname\n";
    out += "workspaces";
    for (auto &name : workspaces)
        out += "\t" + name;
    out += "\ncurrent\t" + std::to_string(currentWs) + "\n";
    static const char *layoutNames[] = {"none", "cascade", "tile", "tilerows"};
    // "active": the windows are cascaded right now, so a restore cascades
    // them again, fitting whatever size the screen has then.
    out += std::string("layout\t") + layoutNames[(int) layout] +
           (cascaded ? "\tactive" : "") + "\n";
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
    bool wasCascaded = false;
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
            wasCascaded = v == "cascade" && fields.size() > 2 && fields[2] == "active";
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
    if (wasCascaded && !cascaded)
        cascadeWindows();
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
    resumeCascade();
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

// The file dialog's folder is kept DOS style ("C:\tmp\"); as a Unix path.
static std::string unixDir(const char *dosDir)
{
    std::string d = dosDir ? dosDir : "";
    if (d.size() >= 2 && d[1] == ':')
        d.erase(0, 2);
    std::replace(d.begin(), d.end(), '\\', '/');
    if (d.empty() || d.back() != '/')
        d += '/';
    return d;
}

// The Attach dialog's file list: Space or Ins marks the file under the
// cursor (and moves down), so several can be attached at once. Marks are
// full paths, so they stay while browsing other folders.
class MarkingFileList : public TFileList
{
public:
    std::set<std::string> &marks;
    TFileDialog &dialog;

    MarkingFileList(const TRect &r, TScrollBar *sb, std::set<std::string> &aMarks,
                    TFileDialog &aDialog) :
        TFileList(r, sb), marks(aMarks), dialog(aDialog) {}

    TSearchRec *entry(short item)
    {
        return list() && item >= 0 && item < list()->getCount()
            ? (TSearchRec *) list()->at(item) : nullptr;
    }

    std::string pathOf(TSearchRec *f) { return unixDir(dialog.directory) + f->name; }

    void getText(char *dest, short item, short maxChars) override
    {
        char name[MAXPATH];
        TFileList::getText(name, item, sizeof(name) - 1);
        TSearchRec *f = entry(item);
        bool marked = f && !(f->attr & 0x10 /* FA_DIREC */) && marks.count(pathOf(f));
        snprintf(dest, maxChars + 1, "%s%s", marked ? "\u221a " : "  ", name);
    }

    void handleEvent(TEvent &ev) override
    {
        if ( ev.what == evKeyDown &&
             (ev.keyDown.keyCode == kbIns || ev.keyDown.charScan.charCode == ' ') )
        {
            if (TSearchRec *f = entry(focused); f && !(f->attr & 0x10 /* FA_DIREC */))
            {
                std::string p = pathOf(f);
                if (!marks.erase(p))
                    marks.insert(p);
            }
            if (focused + 1 < range)
                focusItem(focused + 1);
            drawView();
            clearEvent(ev);
            return;
        }
        TFileList::handleEvent(ev);
    }
};

// With files marked, Open attaches them, whatever the Name field says.
class AttachDialog : public TFileDialog
{
public:
    std::set<std::string> &marks;

    AttachDialog(std::set<std::string> &aMarks) :
        TWindowInit(&TDialog::initFrame),
        TFileDialog("*", "Attach File(s) (Space marks several)", "~N~ame", fdOpenButton, 110),
        marks(aMarks) {}

    Boolean valid(ushort command) override
    {
        if (command == cmFileOpen && !marks.empty())
            return True;
        return TFileDialog::valid(command);
    }
};

// Lets the Attach dialog point its "Files" label at the new list.
struct LabelLink : TLabel
{
    static void relink(TView *v, TView *from, TView *to)
    {
        if (auto *l = dynamic_cast<TLabel *>(v))
            if (static_cast<LabelLink *>(l)->link == from)
                static_cast<LabelLink *>(l)->link = to;
    }
};

void TVTermApp::attachFile()
{
    auto *w = dynamic_cast<TerminalWindow *>(deskTop->current);
    if (!w || !w->isClaude || w->isEnded())
    {
        messageBox(mfInformation | mfOKButton,
            "Select a Claude window first; the file goes to the window in front.");
        return;
    }
    std::string dir = attachDir;
    struct stat st;
    if (dir.empty() || stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        dir = getenv("HOME") ? getenv("HOME") : "/";
    if (dir.size() > 1 && dir.back() == '/')
        dir.pop_back();
    // The dialog opens in the current folder, and only works out typed
    // names right from there: so be in 'dir' while it is open.
    std::string before = currentDir();
    if (chdir(dir.c_str()) != 0)
        return;
    std::set<std::string> marks;
    TFileDialog *d = new AttachDialog(marks);
    {
        TFileList *old = d->fileList;
        auto *list = new MarkingFileList(old->getBounds(), old->vScrollBar, marks, *d);
        list->growMode = old->growMode;
        d->insertBefore(list, old);
        TView *from = old;
        d->forEach([] (TView *v, void *arg) {
            auto **pair = (TView **) arg;
            LabelLink::relink(v, pair[0], pair[1]);
        }, (TView *[]) {from, list});
        d->remove(old);
        TObject::destroy(old);
        d->fileList = list;
        list->readDirectory(d->directory, d->wildCard);
    }
    char path[PATH_MAX] = {};
    ushort result = execView(d);
    if (result != cmCancel)
        d->getData(path);
    TObject::destroy(d);
    if (chdir(before.c_str()) != 0)
        return;
    if (result == cmCancel)
        return;
    std::vector<std::string> files(marks.begin(), marks.end());
    if (files.empty() && *path && stat(path, &st) == 0 && !S_ISDIR(st.st_mode))
        files.push_back(path);
    if (files.empty())
        return;

    size_t slash = files.back().rfind('/');
    if (slash != std::string::npos)
    {
        attachDir = slash ? files.back().substr(0, slash) : "/";
        saveOptions();
    }
    // As a terminal pastes dropped files: shell-escaped, each followed by a
    // space. Claude Code turns each image path into an attachment.
    std::string text;
    for (auto &file : files)
    {
        for (char c : file)
        {
            if (strchr(" \t'\"\\()&;|<>*?$`!#[]{}", c))
                text += '\\';
            text += c;
        }
        text += ' ';
    }

    w->paste(text);
    w->select();
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
    cascadeRect = getTileRect();
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

void TVTermApp::checkTile()
{
    size_t count = 0;
    for (auto *w : sessionWindows())
        if (w->workspace == currentWs)
            ++count;
    bool tiled = layout == Layout::TileCols || layout == Layout::TileRows;
    if (tiled && tiledWs == currentWs && count < tiledCount && count > 0)
        arrange(layout);
    tiledWs = currentWs;
    tiledCount = count;
}

void TVTermApp::resumeCascade()
{
    if (layout != Layout::Cascade)
        return;
    // TDeskTop::cascade puts the front window furthest in, one step a
    // window: the k-th from the front at (n-1-k, n-1-k) from the corner.
    std::vector<TerminalWindow *> visible;
    for (auto *w : sessionWindows())   // Front window first.
        if (w->state & sfVisible)
            visible.push_back(w);
    if (visible.empty())
        return;
    TRect r = getTileRect();
    int n = (int) visible.size();
    for (int k = 0; k < n; ++k)
    {
        TRect want = r;
        want.a.x += n - 1 - k;
        want.a.y += n - 1 - k;
        if (visible[k]->getBounds() != want)
            return;
    }
    cascadeBounds.clear();
    for (auto *w : visible)
        cascadeBounds.push_back({w, w->getBounds()});
    cascadeFront = deskTop->current;
    cascaded = true;
    cascadeRect = getTileRect();
}

void TVTermApp::checkCascade()
{
    if (!cascaded)
        return;
    TView *cur = deskTop->current;
    if (cur && (cur->state & sfDragging))
        return;   // Wait for the drag to end.
    if (getTileRect() != cascadeRect)
    {
        cascadeWindows();   // The screen was resized.
        return;
    }

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
