#ifndef TVTERM_WND_H
#define TVTERM_WND_H

#include <tvterm/termwnd.h>
#include <tvterm/consts.h>
#include "session.h"
#include <string>

class TerminalWindow : public tvterm::BasicTerminalWindow
{
    using Super = tvterm::BasicTerminalWindow;

public:

    static const tvterm::TVTermConstants appConsts;

    TerminalWindow(const TRect &bounds, tvterm::TerminalController &aTerm) noexcept;

    void handleEvent(TEvent &ev) override;
    void sizeLimits(TPoint &min, TPoint &max) override;
    void changeBounds(const TRect &bounds) override;
    void zoom() override;
    const char *getTitle(short maxSize) override;

    // The session this window runs (see session.h).
    int sessionId {0};
    bool isClaude {false};
    std::string name;
    std::string dir;        // The folder the session started in.
    int workspace {0};      // Which workspace (desktop) it belongs to.
    SessionStatus status {SessionStatus::None};

    // Re-reads the status file; true when it changed (the frame is redrawn).
    bool refreshStatus();

    bool isEnded() const noexcept { return isDisconnected(); }

private:

    std::string titleBuf;

    TRect getMaxBounds();
};

inline TerminalWindow::TerminalWindow( const TRect &bounds,
                                       tvterm::TerminalController &aTerm ) noexcept :
    TWindowInit(&initFrame),
    Super(bounds, aTerm, appConsts)
{
}

#endif // TVTERM_WND_H
