#ifndef TVTERM_CMDS_H
#define TVTERM_CMDS_H

#include <tvision/tv.h>

enum : ushort
{
    cmGrabInput = 100,
    cmReleaseInput,
    cmTileCols,
    cmTileRows,
    cmStartSelection,
    // Commands that cannot be deactivated.
    cmNewTerm = 2000,
    cmCheckTerminalUpdates,
    cmTerminalUpdated,
    cmGetOpenTerms,
    cmCopySelection,
    cmCancelSelection,
    cmNewClaude,
    cmNewShell,
    cmSessionList,
    cmNextWaiting,
    cmAbout,
    cmResumeMore,
    cmResume0 = 2100, // ... cmResume0 + 8: File > Resume entries.
    cmWsNew = 2200,
    cmWsRename,
    cmWsClose,
    cmWsNext,
    cmWsPrev,
    cmWs0 = 2210,     // ... cmWs0 + 8: Workspace > entries.
};

enum : ushort
{
    hcMenu = 1000,
    hcInputGrabbed,
    hcSelecting,
};

#endif // TVTERM_CMDS_H
