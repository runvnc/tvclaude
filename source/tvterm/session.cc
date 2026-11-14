#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

const char *statusGlyph(SessionStatus s) noexcept
{
    switch (s)
    {
        case SessionStatus::Working: return "\xE2\x97\x8F"; // ●
        case SessionStatus::Waiting: return "!";
        case SessionStatus::Done:    return "\xE2\x9C\x93"; // ✓
        case SessionStatus::Idle:    return "\xE2\x97\x8B"; // ○
        case SessionStatus::Ended:   return "x";
        default:                     return " ";
    }
}

const char *statusName(SessionStatus s) noexcept
{
    switch (s)
    {
        case SessionStatus::Working: return "working";
        case SessionStatus::Waiting: return "NEEDS YOU";
        case SessionStatus::Done:    return "done";
        case SessionStatus::Idle:    return "idle";
        case SessionStatus::Ended:   return "ended";
        default:                     return "shell";
    }
}

const std::string &statusDir()
{
    static std::string dir = [] {
        const char *base = getenv("XDG_RUNTIME_DIR");
        std::string d = (base && *base) ? base : "/tmp";
        d += "/turbovision-" + std::to_string(getpid());
        mkdir(d.c_str(), 0700);
        return d;
    }();
    return dir;
}

static std::string statusFile(int id)
{
    return statusDir() + "/" + std::to_string(id);
}

SessionStatus readStatus(int id)
{
    FILE *f = fopen(statusFile(id).c_str(), "r");
    if (!f)
        return SessionStatus::None;
    char buf[32] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (!strncmp(buf, "working", 7)) return SessionStatus::Working;
    if (!strncmp(buf, "waiting", 7)) return SessionStatus::Waiting;
    if (!strncmp(buf, "done", 4))    return SessionStatus::Done;
    if (!strncmp(buf, "idle", 4))    return SessionStatus::Idle;
    if (!strncmp(buf, "ended", 5))   return SessionStatus::Ended;
    return SessionStatus::None;
}

// The JSON string value of `"key"` in the first 8 KB of a file; empty when
// missing. Hook payloads are flat at the top, so the first match is the one.
static std::string readHookField(const std::string &path, const char *key)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return {};
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    std::string needle = std::string("\"") + key + "\"";
    const char *p = strstr(buf, needle.c_str());
    if (!p)
        return {};
    p += needle.size();
    while (*p == ' ' || *p == ':')
        ++p;
    if (*p++ != '"')
        return {};
    std::string out;
    for (; *p && *p != '"'; ++p)
    {
        if (*p == '\\' && p[1])
        {
            ++p;
            if (*p == 'u')  // Rare in a path; give up rather than guess.
                return {};
        }
        out += *p;
    }
    return *p == '"' ? out : std::string();
}

std::string readSessionCwd(int id)
{
    std::string cwd = readHookField(statusFile(id) + ".last", "cwd");
    if (cwd.empty())
        cwd = readHookField(statusFile(id) + ".start", "cwd");
    return cwd;
}

std::string readConversationId(int id)
{
    FILE *f = fopen((statusFile(id) + ".start").c_str(), "r");
    if (!f)
        return {};
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char *p = strstr(buf, "\"session_id\"");
    if (!p)
        return {};
    p = strchr(p + 12, ':');
    if (!p || !(p = strchr(p, '"')))
        return {};
    const char *end = strchr(++p, '"');
    if (!end)
        return {};
    std::string sid(p, end);
    // A conversation id is a UUID; refuse anything that is not.
    for (char c : sid)
        if (!isxdigit((unsigned char) c) && c != '-')
            return {};
    return sid;
}

void cleanupStatusDir()
{
    const std::string &dir = statusDir();
    if (DIR *d = opendir(dir.c_str()))
    {
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.')
                unlink((dir + "/" + e->d_name).c_str());
        closedir(d);
    }
    rmdir(dir.c_str());
}

// The folder holding the executable's bundled Claude plugin (the repo's
// plugin/ beside build/), or empty when it is not there.
static const std::string &bundledPluginDir()
{
    static std::string dir = [] {
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return std::string();
        buf[n] = '\0';
        std::string exe = buf;
        std::string base = exe.substr(0, exe.rfind('/'));
        for (const char *rel : {"/../plugin", "/plugin", "/../share/tvclaude/plugin"})
        {
            std::string candidate = base + rel;
            std::string manifest = candidate + "/.claude-plugin/plugin.json";
            if (access(manifest.c_str(), R_OK) == 0)
                if (char *real = realpath(candidate.c_str(), nullptr))
                {
                    std::string r = real;
                    free(real);
                    if (r.find('\'') == std::string::npos)
                        return r;
                }
        }
        return std::string();
    }();
    return dir;
}

// The Claude Code theme every session uses: yellow text, Turbo accents.
// Written to ~/.claude/themes/tvclaude.json (Claude Code reads themes only
// from there); the backgrounds come from tvclaude's own window colors.
static const char themeJson[] = R"({
  "name": "tvclaude",
  "base": "dark",
  "overrides": {
    "claude": "#FFFFFF",
    "claudeShimmer": "#55FFFF",
    "text": "#FFFF55",
    "inverseText": "#000000",
    "inactive": "#AAAAAA",
    "inactiveShimmer": "#FFFFFF",
    "subtle": "#5555FF",
    "suggestion": "#55FFFF",
    "permission": "#AAAAAA",
    "permissionShimmer": "#FFFFFF",
    "remember": "#FF55FF",
    "success": "#55FF55",
    "error": "#FF5555",
    "warning": "#FFFFFF",
    "warningShimmer": "#FFFF55",
    "merged": "#FF55FF",
    "promptBorder": "#00AAAA",
    "promptBorderShimmer": "#55FFFF",
    "planMode": "#55FFFF",
    "autoAccept": "#55FF55",
    "bashBorder": "#FF55FF",
    "ide": "#55FFFF",
    "diffAdded": "#006600",
    "diffRemoved": "#880000",
    "diffAddedDimmed": "#003300",
    "diffRemovedDimmed": "#440000",
    "diffAddedWord": "#00AA00",
    "diffRemovedWord": "#AA0000",
    "userMessageBackground": "#00AAAA",
    "userMessageBackgroundHover": "#55FFFF",
    "bashMessageBackgroundColor": "#AA00AA",
    "memoryBackgroundColor": "#AA5500",
    "selectionBg": "#00AAAA",
    "composerSidebarBackground": "#000060",
    "briefLabelYou": "#55FFFF",
    "briefLabelClaude": "#FFFFFF",
    "rate_limit_fill": "#55FFFF",
    "rate_limit_empty": "#5555FF"
  }
}
)";

void installClaudeTheme()
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return;
    std::string dir = std::string(home) + "/.claude/themes";
    mkdir((std::string(home) + "/.claude").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    std::string path = dir + "/tvclaude.json";

    // Rewrite only when it differs, so Claude Code's watcher stays quiet.
    std::string current;
    if (FILE *f = fopen(path.c_str(), "r"))
    {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            current.append(buf, n);
        fclose(f);
    }
    if (current == themeJson)
        return;
    if (FILE *f = fopen(path.c_str(), "w"))
    {
        fputs(themeJson, f);
        fclose(f);
    }
}

// One hook entry: writes `state` into the session's status file.
static std::string hook(const char *state)
{
    return std::string(R"({"hooks":[{"type":"command","command":"printf )") +
           state + R"( > \"$TURBO_STATUS_FILE\""}]})";
}

// The settings passed with --settings: Claude Code's hooks report the
// session's state to the status file. Nothing in the user's own settings
// files changes.
static std::string hookSettings()
{
    // PreToolUse: a question to the person (AskUserQuestion) means waiting;
    // any other tool means Claude is working again (after a permission).
    std::string preTool =
        R"({"hooks":[{"type":"command","command":"if grep -q AskUserQuestion; then printf waiting; else printf working; fi > \"$TURBO_STATUS_FILE\""}]})";
    // SessionStart: keep Claude's start info (it names the conversation, for
    // restoring the desktop later), then report idle.
    std::string sessionStart =
        R"({"hooks":[{"type":"command","command":"cat > \"$TURBO_STATUS_FILE.start\"; printf idle > \"$TURBO_STATUS_FILE\""}]})";
    // Notification: only a permission prompt or a question dialog needs the
    // person; Claude also notifies when merely idle for a while.
    std::string notification =
        R"({"matcher":"permission_prompt|elicitation_dialog","hooks":[{"type":"command","command":"printf waiting > \"$TURBO_STATUS_FILE\""}]})";
    // Stop: keep the turn's end info too; its "cwd" is the folder Claude is
    // in now, which follows a `cd` (the window is named after it).
    std::string stop =
        R"({"hooks":[{"type":"command","command":"cat > \"$TURBO_STATUS_FILE.last\"; printf done > \"$TURBO_STATUS_FILE\""}]})";
    return std::string("{\"theme\":\"custom:tvclaude\",\"hooks\":{") +
        "\"SessionStart\":[" + sessionStart + "]," +
        "\"UserPromptSubmit\":[" + hook("working") + "]," +
        "\"PreToolUse\":[" + preTool + "]," +
        "\"PostToolUse\":[" + hook("working") + "]," +
        "\"Notification\":[" + notification + "]," +
        "\"Stop\":[" + stop + "]," +
        "\"SessionEnd\":[" + hook("ended") + "]" +
        "}}";
}

SessionEmulatorFactory::SessionEmulatorFactory(const SessionSpec &spec)
{
    storage.reserve(8);
    auto add = [&] (const char *name, std::string value) {
        storage.push_back(std::move(value));
        env.push_back({name, nullptr});
    };
    for (const auto &v : inner.getCustomEnvironment())
        env.push_back(v);
    add("TURBOVISION", "1");
    if (!spec.dir.empty())
        add("TURBO_CWD", spec.dir);
    if (spec.isClaude)
    {
        add("TURBO_STATUS_FILE", statusFile(spec.id));
        std::string cmd = "exec claude --settings '" + hookSettings() + "'";
        const std::string &plugin = bundledPluginDir();
        if (!plugin.empty())
            cmd += " --plugin-dir '" + plugin + "'";
        if (!spec.resumeId.empty() && spec.resumeId.find('\'') == std::string::npos)
            cmd += " --resume '" + spec.resumeId + "'";
        else if (spec.continueLast)
            cmd += " --continue";
        add("TURBO_CMD", cmd);
    }
    // Point each added variable at its stored string (stable after reserve).
    size_t first = env.size() - storage.size();
    for (size_t i = 0; i < storage.size(); ++i)
        env[first + i].value = storage[i].c_str();
}

tvterm::TerminalEmulator &SessionEmulatorFactory::create(TPoint size, tvterm::Writer &w) noexcept
{
    return inner.create(size, w);
}

TSpan<const tvterm::EnvironmentVar> SessionEmulatorFactory::getCustomEnvironment() noexcept
{
    return {env.data(), env.size()};
}
