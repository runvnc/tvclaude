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
    titleBuf += name.empty() ? "Terminal" : name;
    if (isClaude && status != SessionStatus::None)
    {
        titleBuf += " [";
        titleBuf += statusName(status);
        titleBuf += ']';
    }
    if (term && *term)
    {
        titleBuf += " - ";
        titleBuf += term;
    }
    return titleBuf.c_str();
}

bool TerminalWindow::refreshStatus()
{
    if (!isClaude)
        return false;
    SessionStatus s = isDisconnected() ? SessionStatus::Ended : readStatus(sessionId);
    if (s == status)
        return false;
    status = s;
    if (frame)
        frame->drawView();
    return true;
}
