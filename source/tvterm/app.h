#ifndef TVTERM_APP_H
#define TVTERM_APP_H

#define Uses_TApplication
#define Uses_TCommandSet
#include <tvision/tv.h>

#include "session.h"
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

    void newSession(const SessionSpec &spec);
    void newClaudeDialog();
    void sessionList();
    void nextWaiting();
    void changeDir();
    void about();

private:

    SessionIndicator *indicator {nullptr};
    int nextSessionId {1};
    std::string lastDir;
    long long lastPollMs {0};

    void pollStatuses();
    short freeWindowNumber();
};

inline TVTermDesk* TVTermApp::getDeskTop()
{
    return (TVTermDesk*) deskTop;
}

#endif // TVTERM_APP_H
