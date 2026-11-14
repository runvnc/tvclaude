#include "wnd.h"
#include "cmds.h"

#define Uses_TEvent
#define Uses_TFrame
#include <tvision/tv.h>

const tvterm::TVTermConstants TerminalWindow::appConsts =
{
    cmCheckTerminalUpdates,
    cmTerminalUpdated,
    cmCopySelection,
    cmCancelSelection,
    cmGrabInput,
    cmReleaseInput,
    cmStartSelection,
    hcInputGrabbed,
    hcSelecting,
};

void TerminalWindow::handleEvent(TEvent &ev)
{
    // Alt-1..9 picks a window of the workspace on screen, not a hidden one.
    if ( ev.what == evBroadcast && ev.message.command == cmSelectWindowNum &&
         !(state & sfVisible) )
        return;
    if ( ev.what == evBroadcast &&
         ev.message.command == cmGetOpenTerms && !isDisconnected() )
        *(size_t *) ev.message.infoPtr += 1;
    else if ( ev.what == evKeyDown && isDisconnected() &&
              !(state & (sfDragging | sfModal)) )
    {
        close();
        return;
    }
    Super::handleEvent(ev);
}

void TerminalWindow::sizeLimits(TPoint &min, TPoint &max)
{
    Super::sizeLimits(min, max);
    if (owner)
    {
        max = owner->size;
        max.x += 2;
        max.y += 1;
    }
}

void TerminalWindow::changeBounds(const TRect &bounds)
{
    TRect maxBounds = getMaxBounds();
    if (bounds == maxBounds)
    {
        // A maximized terminal shows just the title bar and cannot be dragged.
        growMode = gfGrowHiX | gfGrowHiY;
        flags &= ~(wfMove | wfGrow);
    }
    else
    {
        growMode = gfGrowAll | gfGrowRel;
        flags |= wfMove | wfGrow;
    }
    Super::changeBounds(bounds);
}

void TerminalWindow::zoom()
{
    TRect bounds = getBounds();
    TRect maxBounds = getMaxBounds();
    if (bounds != maxBounds)
    {
        zoomRect = bounds;
        locate(maxBounds);
    }
    else
        locate(zoomRect);
}

TRect TerminalWindow::getMaxBounds()
{
    TPoint minSize, maxSize;
    sizeLimits(minSize, maxSize);
    TRect r(0, 0, maxSize.x, maxSize.y);
    r.move(-1, 0);
    return r;
}

const char *TerminalWindow::getTitle(short maxSize)
{
    const char *term = Super::getTitle(maxSize);
    titleBuf.clear();
    if (isClaude)
    {
        titleBuf += statusGlyph(status);
        titleBuf += ' ';
    }
    titleBuf += displayName().empty() ? "Terminal" : displayName();
    if (isClaude && status != SessionStatus::None)
    {
        titleBuf += " [";
        titleBuf += statusName(status);
        titleBuf += ']';
    }
    if (isClaude && !description.get().empty())
    {
        titleBuf += " - ";
        titleBuf += description.get();
    }
    else if (term && *term)
    {
        titleBuf += " - ";
        titleBuf += term;
    }
    return titleBuf.c_str();
}

void TerminalWindow::setCustomName(std::string n)
{
    customName = std::move(n);
    if (frame)
        frame->drawView();
}

static std::string lastPart(const std::string &path)
{
    size_t end = path.find_last_not_of('/');
    if (end == std::string::npos)
        return path.empty() ? path : "/";
    size_t slash = path.rfind('/', end);
    return path.substr(slash == std::string::npos ? 0 : slash + 1,
                       end - (slash == std::string::npos ? 0 : slash + 1) + 1);
}

bool TerminalWindow::refreshStatus()
{
    if (!isClaude)
        return false;
    std::string now = readSessionCwd(sessionId);
    if (!now.empty() && now != cwd)
    {
        cwd = now;
        name = lastPart(cwd);
        if (customName.empty() && frame)
            frame->drawView();
    }
    if (description.update(dir, readConversationId(sessionId)) && frame)
        frame->drawView();
    SessionStatus s = isDisconnected() ? SessionStatus::Ended : readStatus(sessionId);
    if (s == status)
        return false;
    status = s;
    if (frame)
        frame->drawView();
    return true;
}
