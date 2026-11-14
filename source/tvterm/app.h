#ifndef TVTERM_APP_H
#define TVTERM_APP_H

#define Uses_TApplication
#define Uses_TCommandSet
#include <tvision/tv.h>

#include "session.h"
#include "history.h"
#include <string>
#include <vector>

class TVTermDesk;
class TerminalWindow;
class SessionIndicator;

struct TVTermApp : public TApplication
{
    static TCommandSet tileCmds;

    TVTermApp(const std::vector<std::string> &dirs);
    static TStatusLine* initStatusLine(TRect r);
    static TMenuBar* initMenuBar(TRect r);
    static TDeskTop* initDeskTop(TRect r);

    TVTermDesk* getDeskTop();

    void handleEvent(TEvent &event) override;
    Boolean valid(ushort command) override;
    void idle() override;
    void shutDown() override;
    TPalette &getPalette() const override;

    size_t getOpenTermCount();
    std::vector<TerminalWindow *> sessionWindows();

    // Command handlers

    TerminalWindow *newSession(const SessionSpec &spec);
    void newClaudeDialog();
    void sessionList();
    void nextWaiting();
    void changeDir();
    void about();
    void resume(size_t index);
    void resumeDialog();

    // Fills File > Resume with the newest past sessions; called as the menu
    // bar is about to open.
    void refreshResumeMenu();

    // Workspaces: separate desktops of windows, e.g. one per project.
    // Switching hides one set and shows another; every session keeps running.
    void refreshWorkspaceMenu();
    void switchWorkspace(int index);
    void newWorkspaceDialog();
    void renameWorkspace();
    void closeWorkspace();
    const std::string &workspaceName() const { return workspaces[currentWs]; }

    // Cascade that stays cascaded: bringing a window forward re-cascades in
    // the new order, so every title bar stays in view. Moving or resizing a
    // window by hand ends it.
    void cascadeWindows();
    void checkCascade();

    // The desktop file (~/.config/tvclaude/desktop): every open window, its
    // folder, place and Claude conversation, saved every few seconds and on
    // exit. A start with no folders given reopens it.
    void saveDesktop();
    bool restoreDesktop();

private:

    SessionIndicator *indicator {nullptr};
    int nextSessionId {1};
    std::string lastDir;
    long long lastPollMs {0};
    std::vector<PastSession> resumeEntries;
    std::string lastSavedDesktop;
    long long lastSaveMs {0};
    std::vector<std::string> workspaces {"Main"};
    int currentWs {0};
    bool cascaded {false};
    TView *cascadeFront {nullptr};
    std::vector<std::pair<TView *, TRect>> cascadeBounds;

    void pollStatuses();
    short freeWindowNumber();
};

inline TVTermDesk* TVTermApp::getDeskTop()
{
    return (TVTermDesk*) deskTop;
}

#endif // TVTERM_APP_H
