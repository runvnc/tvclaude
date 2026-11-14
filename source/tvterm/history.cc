#include "history.h"

#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

namespace
{

struct CacheEntry
{
    time_t mtime {0};
    off_t size {0};
    PastSession session;
    bool hasContent {false};
};

std::map<std::string, CacheEntry> cache;

void appendUtf8(std::string &out, unsigned cp)
{
    if (cp < 0x80)
        out += (char) cp;
    else if (cp < 0x800)
    {
        out += (char) (0xC0 | (cp >> 6));
        out += (char) (0x80 | (cp & 0x3F));
    }
    else
    {
        out += (char) (0xE0 | (cp >> 12));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
}

// Reads the JSON string starting just after its opening quote.
std::string jsonString(const char *p, const char *end)
{
    std::string out;
    while (p < end && *p != '"')
    {
        if (*p == '\\' && p + 1 < end)
        {
            ++p;
            switch (*p)
            {
                case 'n': case 'r': case 't': out += ' '; break;
                case 'u':
                    if (p + 4 < end)
                    {
                        char hex[5] = {p[1], p[2], p[3], p[4], 0};
                        unsigned cp = (unsigned) strtoul(hex, nullptr, 16);
                        if (cp < 0xD800 || cp > 0xDFFF) // Skip surrogate halves.
                            appendUtf8(out, cp);
                        p += 4;
                    }
                    break;
                default: out += *p; break;
            }
            ++p;
        }
        else
            out += *p++;
    }
    return out;
}

// The value of the last `"key":"..."` in the text, or empty.
std::string lastValue(const std::string &text, const char *key)
{
    std::string needle = std::string("\"") + key + "\":\"";
    size_t at = text.rfind(needle);
    if (at == std::string::npos)
        return {};
    const char *p = text.data() + at + needle.size();
    return jsonString(p, text.data() + text.size());
}

// Claude Code files a session under its folder with every character other
// than a letter or digit replaced by '-'.
std::string projectKey(const std::string &dir)
{
    std::string k = dir;
    for (char &c : k)
        if (!isalnum((unsigned char) c))
            c = '-';
    return k;
}

// The folder the session belongs to: the recorded `cwd` that matches its
// project folder (a session may have moved), else the first one.
std::string sessionDir(const std::string &text, const std::string &project)
{
    static const std::string needle = "\"cwd\":\"";
    std::string first;
    for (size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1))
    {
        const char *p = text.data() + at + needle.size();
        std::string cwd = jsonString(p, text.data() + text.size());
        if (projectKey(cwd) == project)
            return cwd;
        if (first.empty())
            first = cwd;
    }
    return first;
}

void readSession(const std::string &path, const std::string &project,
                 const std::string &id, CacheEntry &entry)
{
    entry.session = {};
    entry.hasContent = false;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);

    PastSession &s = entry.session;
    s.id = id;
    s.cwd = sessionDir(text, project);
    s.title = lastValue(text, "customTitle");
    if (s.title.empty())
        s.title = lastValue(text, "aiTitle");
    if (s.title.empty())
        s.title = lastValue(text, "lastPrompt");
    entry.hasContent = !s.cwd.empty() && !s.title.empty();
}

} // namespace

std::vector<PastSession> listPastSessions()
{
    std::vector<PastSession> list;
    const char *home = getenv("HOME");
    if (!home)
        return list;
    std::string root = std::string(home) + "/.claude/projects";
    DIR *projects = opendir(root.c_str());
    if (!projects)
        return list;
    while (dirent *p = readdir(projects))
    {
        if (p->d_name[0] == '.')
            continue;
        std::string dir = root + "/" + p->d_name;
        DIR *d = opendir(dir.c_str());
        if (!d)
            continue;
        while (dirent *e = readdir(d))
        {
            std::string name = e->d_name;
            if (name.size() < 7 || name.compare(name.size() - 6, 6, ".jsonl") != 0)
                continue;
            std::string path = dir + "/" + name;
            struct stat st;
            if (stat(path.c_str(), &st) != 0)
                continue;
            CacheEntry &entry = cache[path];
            if (entry.mtime != st.st_mtime || entry.size != st.st_size)
            {
                entry.mtime = st.st_mtime;
                entry.size = st.st_size;
                readSession(path, p->d_name, name.substr(0, name.size() - 6), entry);
            }
            if (entry.hasContent)
            {
                PastSession s = entry.session;
                s.modified = st.st_mtime;
                list.push_back(std::move(s));
            }
        }
        closedir(d);
    }
    closedir(projects);
    std::sort(list.begin(), list.end(), [] (auto &a, auto &b) {
        return a.modified > b.modified;
    });
    return list;
}

std::string ageText(time_t when)
{
    time_t now = time(nullptr);
    long d = (long) difftime(now, when);
    char buf[32];
    if (d < 60)
        return "now";
    if (d < 3600)
        snprintf(buf, sizeof(buf), "%ldm", d / 60);
    else if (d < 86400)
        snprintf(buf, sizeof(buf), "%ldh", d / 3600);
    else if (d < 7 * 86400)
        snprintf(buf, sizeof(buf), "%ldd", d / 86400);
    else
    {
        struct tm tm;
        localtime_r(&when, &tm);
        strftime(buf, sizeof(buf), "%b %e", &tm);
    }
    return buf;
}
