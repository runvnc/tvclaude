#include "theme.h"
#include "session.h"

#include <tvterm/vtermemu.h>

#include <stdio.h>

// The VGA text-mode colors, in VGA order (black, blue, green, cyan, red,
// magenta, brown, light gray, then the bright ones).
static const unsigned vga[16] =
{
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

// The same, in ANSI order (black, red, green, yellow, blue, magenta, cyan,
// white, then the bright ones).
#define VGA_ANSI { 0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA, \
                   0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF }

const Theme themes[] =
{
    // ---- Retro ----------------------------------------------------------
    // id, name, retro, light, vga,
    // desk, deskFg, pattern, bg, fg, bright, dim, frame,
    // surface, surfaceFg, hot, sel, accent, accentBg, shadow, termBg, ansi
    {"turbo", "Turbo Vision", true, false, true,
     0xAAAAAA, 0x0000AA, '\xB0', 0x0000AA, 0xFFFF55, 0xFFFFFF, 0x555555, 0xAAAAAA,
     0xAAAAAA, 0x000000, 0xAA0000, 0x00AA00, 0x55FFFF, 0x00AAAA, 0x000000, 0x000080,
     VGA_ANSI},
    // The same layout in muted colors: a soft navy, warm cream text and
    // gentle grays instead of saturated blue and bright yellow.
    {"turbo-soft", "Turbo Vision Soft", true, false, false,
     0x323C55, 0x3B4663, '\xB0', 0x1F2A45, 0xE3D49A, 0xE6E8EC, 0x6F7686, 0x98A1B3,
     0xBCC1CA, 0x1B2030, 0x9E3B3B, 0x7FA88A, 0x8CC7CE, 0x9FBFC4, 0x11172A, 0x1B2540,
     {0x1B2030, 0xD27070, 0x93C493, 0xE3D49A, 0x7D9AD6, 0xC392CB, 0x8CC7CE, 0xC9CDD5,
      0x5C6478, 0xE89090, 0xAEDBAE, 0xF0E4B0, 0x9DB5EA, 0xDBB0E3, 0xA8DCE2, 0xF2F3F5}},
    {"norton", "Norton Commander", true, false, false,
     0x000000, 0x000000, ' ', 0x0000AA, 0x55FFFF, 0xFFFFFF, 0x555555, 0x55FFFF,
     0x00AAAA, 0x000000, 0xFFFFFF, 0xAAAAAA, 0xFFFF55, 0x00AAAA, 0x000000, 0x0000AA,
     VGA_ANSI},
    {"c64", "Commodore 64", true, false, false,
     0x6C5EB5, 0x6C5EB5, ' ', 0x352879, 0x8A7FD8, 0xFFFFFF, 0x6C6C6C, 0x959595,
     0x6C5EB5, 0x1F1650, 0xFFFFFF, 0xB8B0F0, 0x70A4B2, 0x8F84D4, 0x1F1650, 0x352879,
     {0x000000, 0x9A6759, 0x588D43, 0xB8C76F, 0x6C5EB5, 0x6F3D86, 0x70A4B2, 0x959595,
      0x444444, 0xC08070, 0x9AD284, 0xD8E090, 0x8A7FD8, 0xA070C0, 0x90C8D8, 0xFFFFFF}},
    {"green", "Green phosphor", true, false, false,
     0x020A02, 0x0A2A0A, '\xB0', 0x031203, 0x33E833, 0xA8FFA8, 0x1A6B1A, 0x26B026,
     0x2FCC2F, 0x001A00, 0xE6FFE6, 0x6EE86E, 0x66FF88, 0x26B026, 0x000000, 0x031203,
     {0x031203, 0x33E833, 0x33E833, 0x66FF66, 0x26B026, 0x66FF66, 0x66FF66, 0x99FF99,
      0x1A6B1A, 0x66FF66, 0x66FF66, 0x99FF99, 0x33E833, 0x99FF99, 0x99FF99, 0xCCFFCC}},
    {"amber", "Amber phosphor", true, false, false,
     0x0A0600, 0x2A1A00, '\xB0', 0x120B00, 0xFFB000, 0xFFDA8A, 0x7A5200, 0xC78A00,
     0xE09A00, 0x1A0F00, 0xFFF2CC, 0xF5C04A, 0xFFCC55, 0xC78A00, 0x000000, 0x120B00,
     {0x120B00, 0xFFB000, 0xFFB000, 0xFFC840, 0xC78A00, 0xFFC840, 0xFFC840, 0xFFD480,
      0x7A5200, 0xFFC840, 0xFFC840, 0xFFD480, 0xFFB000, 0xFFD480, 0xFFD480, 0xFFEBC0}},

    // ---- Modern ---------------------------------------------------------
    {"catppuccin-mocha", "Catppuccin Mocha", false, false, false,
     0x11111B, 0x11111B, ' ', 0x1E1E2E, 0xCDD6F4, 0xB4BEFE, 0x6C7086, 0x9399B2,
     0x313244, 0xCDD6F4, 0xFAB387, 0x585B70, 0x89DCEB, 0x45475A, 0x0B0B12, 0,
     {0x45475A, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xBAC2DE,
      0x585B70, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xA6ADC8}},
    {"catppuccin-latte", "Catppuccin Latte (light)", false, true, false,
     0xDCE0E8, 0xDCE0E8, ' ', 0xEFF1F5, 0x4C4F69, 0x303446, 0x9CA0B0, 0x8C8FA1,
     0xE6E9EF, 0x4C4F69, 0xFE640B, 0xBCC0CC, 0x04A5E5, 0xCCD0DA, 0xACB0BE, 0,
     {0x5C5F77, 0xD20F39, 0x40A02B, 0xDF8E1D, 0x1E66F5, 0xEA76CB, 0x179299, 0xACB0BE,
      0x6C6F85, 0xD20F39, 0x40A02B, 0xDF8E1D, 0x1E66F5, 0xEA76CB, 0x179299, 0xBCC0CC}},
    {"tokyo-night", "Tokyo Night", false, false, false,
     0x16161E, 0x16161E, ' ', 0x1A1B26, 0xC0CAF5, 0xE0E6FF, 0x565F89, 0xA9B1D6,
     0x24283B, 0xC0CAF5, 0xFF9E64, 0x3D59A1, 0x7DCFFF, 0x292E42, 0x0F0F14, 0,
     {0x15161E, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xA9B1D6,
      0x414868, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5}},
    {"dracula", "Dracula", false, false, false,
     0x191A21, 0x191A21, ' ', 0x282A36, 0xF8F8F2, 0xFFFFFF, 0x6272A4, 0xB0B4C8,
     0x44475A, 0xF8F8F2, 0xFFB86C, 0x6272A4, 0x8BE9FD, 0x3A3C4E, 0x0E0F14, 0,
     {0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
      0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF}},
    {"gruvbox", "Gruvbox Dark", false, false, false,
     0x1D2021, 0x1D2021, ' ', 0x282828, 0xEBDBB2, 0xFBF1C7, 0x928374, 0xA89984,
     0x3C3836, 0xEBDBB2, 0xFE8019, 0x665C54, 0x8EC07C, 0x504945, 0x141617, 0,
     {0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984,
      0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2}},
    {"nord", "Nord", false, false, false,
     0x242933, 0x242933, ' ', 0x2E3440, 0xD8DEE9, 0xECEFF4, 0x616E88, 0xAEB7C6,
     0x3B4252, 0xE5E9F0, 0xEBCB8B, 0x5E81AC, 0x88C0D0, 0x434C5E, 0x1C2028, 0,
     {0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,
      0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4}},
    {"one-dark", "One Dark", false, false, false,
     0x21252B, 0x21252B, ' ', 0x282C34, 0xABB2BF, 0xDCDFE4, 0x5C6370, 0x9DA5B4,
     0x333842, 0xC8CCD4, 0xD19A66, 0x4D78CC, 0x56B6C2, 0x3E4451, 0x181A1F, 0,
     {0x282C34, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xABB2BF,
      0x5C6370, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xFFFFFF}},
    {"rose-pine", "Rose Pine", false, false, false,
     0x12101A, 0x12101A, ' ', 0x191724, 0xE0DEF4, 0xF6F4FF, 0x6E6A86, 0x908CAA,
     0x26233A, 0xE0DEF4, 0xF6C177, 0x524F67, 0x9CCFD8, 0x403D52, 0x0C0B12, 0,
     {0x26233A, 0xEB6F92, 0x31748F, 0xF6C177, 0x9CCFD8, 0xC4A7E7, 0xEBBCBA, 0xE0DEF4,
      0x6E6A86, 0xEB6F92, 0x31748F, 0xF6C177, 0x9CCFD8, 0xC4A7E7, 0xEBBCBA, 0xE0DEF4}},
    {"solarized-dark", "Solarized Dark", false, false, false,
     0x00212B, 0x00212B, ' ', 0x002B36, 0x839496, 0xEEE8D5, 0x586E75, 0x93A1A1,
     0x073642, 0x93A1A1, 0xCB4B16, 0x245A6A, 0x2AA198, 0x0A4050, 0x001820, 0,
     {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
      0x586E75, 0xCB4B16, 0x859900, 0xB58900, 0x268BD2, 0x6C71C4, 0x2AA198, 0xFDF6E3}},
    {"solarized-light", "Solarized Light (light)", false, true, false,
     0xEEE8D5, 0xEEE8D5, ' ', 0xFDF6E3, 0x657B83, 0x073642, 0x93A1A1, 0x586E75,
     0xEEE8D5, 0x586E75, 0xCB4B16, 0xC9DDE3, 0x2AA198, 0xE4DFCC, 0xD6CFB8, 0,
     {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
      0x586E75, 0xCB4B16, 0x859900, 0xB58900, 0x268BD2, 0x6C71C4, 0x2AA198, 0x002B36}},
};

const size_t themeCount = sizeof(themes) / sizeof(themes[0]);

static size_t current = 0;

const Theme &currentTheme() noexcept
{
    return themes[current];
}

size_t currentThemeIndex() noexcept
{
    return current;
}

static TColorRGB rgb(unsigned c)
{
    return TColorRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static unsigned blend(unsigned a, unsigned b, double t)
{
    unsigned out = 0;
    for (int shift = 16; shift >= 0; shift -= 8)
    {
        double x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
        out |= (unsigned) (x + 0.5) << shift;
    }
    return out;
}

static unsigned termBg(const Theme &t)
{
    return t.termBg ? t.termBg : t.bg;
}

// What each VGA color becomes as a foreground and as a background.
static unsigned fgColor(const Theme &t, int i)
{
    if (t.vga)
        return vga[i];
    switch (i)
    {
        case 0: return t.surfaceFg;
        case 1: return t.ansi[4];
        case 2: return t.ansi[2];
        case 3: return t.accent;
        case 4: return t.hot;
        case 5: return t.ansi[5];
        case 6: return t.ansi[3];
        case 7: return t.frame;
        case 8: return t.dim;
        case 9: return t.ansi[12];
        case 10: return t.ansi[10];
        case 11: return t.accent;
        case 12: return t.ansi[9];
        case 13: return t.ansi[13];
        case 14: return t.fg;
        default: return t.bright;
    }
}

static unsigned bgColor(const Theme &t, int i)
{
    if (t.vga)
        return vga[i];
    switch (i)
    {
        case 0: return t.shadow;
        case 1: return t.bg;
        case 2: return t.sel;
        case 3: return t.accentBg;
        case 4: return blend(t.bg, t.ansi[1], 0.5);
        case 5: return blend(t.bg, t.ansi[5], 0.5);
        case 6: return blend(t.bg, t.ansi[3], 0.5);
        case 7: return t.surface;
        case 8: return t.dim;
        default:
        {
            // 9..15: bright blue, green, cyan, red, magenta, yellow, white.
            static const int ansiOf[] = {12, 10, 14, 9, 13, 11, 15};
            return blend(t.bg, t.ansi[ansiOf[i - 9]], 0.6);
        }
    }
}

TColorAttr themeAttr(unsigned char bios) noexcept
{
    const Theme &t = currentTheme();
    return TColorAttr(rgb(fgColor(t, bios & 0xF)), rgb(bgColor(t, bios >> 4)));
}

// The Claude Code theme that matches: Claude's text and accents in the
// theme's colors. The classic theme keeps the hand-made Turbo one.
static const char turboJson[] = R"({
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

static std::string hex(unsigned c)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", c & 0xFFFFFF);
    return buf;
}

static std::string claudeJson(const Theme &t)
{
    if (t.vga)
        return turboJson;
    unsigned bg = termBg(t);
    unsigned red = t.ansi[9], green = t.ansi[10], yellow = t.ansi[11],
             magenta = t.ansi[13];
    // The plugin draws the person's prompts in inverseText on promptBorder.
    std::pair<const char *, unsigned> keys[] =
    {
        {"claude", t.hot},
        {"claudeShimmer", t.bright},
        {"text", t.fg},
        {"inverseText", bg},
        {"inactive", t.frame},
        {"inactiveShimmer", t.bright},
        {"subtle", t.dim},
        {"suggestion", t.accent},
        {"permission", t.frame},
        {"permissionShimmer", t.bright},
        {"remember", magenta},
        {"success", green},
        {"error", red},
        {"warning", yellow},
        {"warningShimmer", t.bright},
        {"merged", magenta},
        {"promptBorder", t.accent},
        {"promptBorderShimmer", t.bright},
        {"planMode", t.accent},
        {"autoAccept", green},
        {"bashBorder", magenta},
        {"ide", t.accent},
        {"diffAdded", blend(bg, green, 0.3)},
        {"diffRemoved", blend(bg, red, 0.3)},
        {"diffAddedDimmed", blend(bg, green, 0.15)},
        {"diffRemovedDimmed", blend(bg, red, 0.15)},
        {"diffAddedWord", blend(bg, green, 0.55)},
        {"diffRemovedWord", blend(bg, red, 0.55)},
        {"userMessageBackground", blend(bg, t.accent, 0.25)},
        {"userMessageBackgroundHover", blend(bg, t.accent, 0.4)},
        {"bashMessageBackgroundColor", blend(bg, magenta, 0.25)},
        {"memoryBackgroundColor", blend(bg, yellow, 0.25)},
        {"selectionBg", t.sel},
        {"composerSidebarBackground", t.desk},
        {"briefLabelYou", t.accent},
        {"briefLabelClaude", t.bright},
        {"rate_limit_fill", t.accent},
        {"rate_limit_empty", t.dim},
    };
    std::string out = std::string("{\n  \"name\": \"tvclaude\",\n  \"base\": \"") +
                      (t.light ? "light" : "dark") + "\",\n  \"overrides\": {\n";
    for (size_t i = 0; i < std::size(keys); ++i)
        out += std::string("    \"") + keys[i].first + "\": \"" + hex(keys[i].second) +
               (i + 1 < std::size(keys) ? "\",\n" : "\"\n");
    out += "  }\n}\n";
    return out;
}

// Turbo Vision's shadow color (tview.cpp): drawn over whatever is behind.
extern TColorAttr shadowAttr;

static void apply(const Theme &t)
{
    // Dark enough that tvision's reversed shadow on black areas stays dark.
    shadowAttr = t.vga ? TColorAttr(0x08)
                       : TColorAttr(rgb(blend(t.shadow, t.dim, 0.4)), rgb(t.shadow));
    static TColorRGB ansi[16];
    for (int i = 0; i < 16; ++i)
        ansi[i] = rgb(t.ansi[i]);
    // The classic theme leaves ANSI colors to the host terminal, as before.
    tvterm::setAnsiColors(t.vga ? nullptr : ansi);
    tvterm::setDefaultColors(rgb(t.fg), rgb(termBg(t)));
    installClaudeTheme(claudeJson(t));
}

bool selectTheme(size_t index) noexcept
{
    static bool applied = false;
    if (index >= themeCount || (applied && index == current))
        return false;
    current = index;
    applied = true;
    apply(themes[index]);
    return true;
}

bool selectTheme(const std::string &id) noexcept
{
    for (size_t i = 0; i < themeCount; ++i)
        if (id == themes[i].id)
            return selectTheme(i);
    return false;
}
