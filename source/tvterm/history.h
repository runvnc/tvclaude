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

// The newest `limit` past sessions with something to show, newest first.
// Only those files are read, and only when they changed since last time.
std::vector<PastSession> listPastSessions(size_t limit);

// Follows one conversation's title as Claude writes it, the way /resume
// shows it: its custom title, else its AI title, else its last prompt. Only
// what was added to the history file since the last call is read.
class ConversationTitle
{
public:
    // `dir` is the folder the session started in; `id` its conversation id.
    // True when the title changed.
    bool update(const std::string &dir, const std::string &id);
    const std::string &get() const noexcept { return title; }

private:
    std::string id, path, partial;
    long long offset {0};
    time_t lastLookup {0};
    std::string custom, ai, prompt, title;
};

// "5m", "3h", "2d", "Nov 3": how long ago.
std::string ageText(time_t when);

#endif // TVTERM_HISTORY_H
