#ifndef TVTERM_SESSION_H
#define TVTERM_SESSION_H

#include <tvterm/termemu.h>
#include <tvterm/vtermemu.h>
#include <string>
#include <vector>

// What a Claude session is doing, as its hooks last reported it.
enum class SessionStatus
{
    None,       // A plain shell, or Claude has not reported yet.
    Idle,       // Started, nothing asked yet.
    Working,    // Claude is on a turn.
    Waiting,    // Claude needs the person: a permission prompt or a question.
    Done,       // The turn finished; the answer is waiting to be read.
    Ended,      // The process exited.
};

const char *statusGlyph(SessionStatus) noexcept;
const char *statusName(SessionStatus) noexcept;

// What to start in a new window.
struct SessionSpec
{
    bool isClaude {true};
    std::string dir;        // Working folder; empty for the current one.
    bool continueLast {false};
    std::string resumeId;   // `claude --resume <id>` when not empty.
    int id {0};             // Unique per process; names the status file.
};

// A terminal emulator factory that hands the child the session's folder,
// command and status-file location through the environment (see pty.cc).
class SessionEmulatorFactory : public tvterm::TerminalEmulatorFactory
{
    tvterm::VTermEmulatorFactory inner;
    std::vector<std::string> storage;
    std::vector<tvterm::EnvironmentVar> env;

public:

    explicit SessionEmulatorFactory(const SessionSpec &spec);

    tvterm::TerminalEmulator &create(TPoint size, tvterm::Writer &w) noexcept override;
    TSpan<const tvterm::EnvironmentVar> getCustomEnvironment() noexcept override;
};

// Where status files live: $XDG_RUNTIME_DIR/turbovision-<pid>, created once.
const std::string &statusDir();

// Reads the status file a session's hooks write. Cheap; called from idle().
SessionStatus readStatus(int id);

// Writes the tvclaude Claude Code theme to ~/.claude/themes when missing
// or out of date. Sessions select it with --settings.
void installClaudeTheme();

// The id of the Claude conversation session `id` is running (from its
// SessionStart hook), for `claude --resume`; empty when unknown.
std::string readConversationId(int id);

// Removes the status files of this process.
void cleanupStatusDir();

#endif // TVTERM_SESSION_H
