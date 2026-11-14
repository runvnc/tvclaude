#ifndef TVTERM_HISTORY_H
#define TVTERM_HISTORY_H

#include <string>
#include <vector>
#include <time.h>

// One past Claude Code conversation, as `claude --resume` lists them: read
// from ~/.claude/projects/<folder>/<session id>.jsonl.
struct PastSession
{
    std::string id;      // The session id `claude --resume` takes.
    std::string cwd;     // The folder it ran in.
    std::string title;   // Its title, or the last prompt when it has none.
    time_t modified {0};
};

// Every past session with something to show, newest first. Files are
// re-read only when they changed since the last call.
std::vector<PastSession> listPastSessions();

// "5m", "3h", "2d", "Nov 3": how long ago.
std::string ageText(time_t when);

#endif // TVTERM_HISTORY_H
