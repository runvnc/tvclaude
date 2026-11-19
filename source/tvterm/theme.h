#ifndef TVCLAUDE_THEME_H
#define TVCLAUDE_THEME_H

#define Uses_TColorAttr
#include <tvision/tv.h>

#include <string>

// A color scheme for the whole desktop: Turbo Vision's own colors, the text
// inside the session windows, and the matching Claude Code theme.
//
// Turbo Vision names its colors as text-mode attributes (16 VGA colors, the
// background in the high nibble). A theme repaints those: each VGA color
// gets one RGB value when used as a foreground and another as a background,
// so "black on light gray" (menus, dialogs) can become light on dark, and so
// on, while every view keeps its own palette.
struct Theme
{
    const char *id;     // Saved in the options file.
    const char *name;   // Shown in Options > Theme.
    bool retro;         // Which submenu it is in.
    bool light;         // Claude Code's base theme: light or dark.
    bool vga;           // Exact VGA colors (the classic Turbo Vision look).

    // Roles, as 0xRRGGBB.
    unsigned desk, deskFg;   // The desktop behind the windows.
    char pattern;            // The desktop's fill character.
    unsigned bg, fg;         // Windows and their text (VGA blue, yellow).
    unsigned bright;         // Titles, active frames (VGA white).
    unsigned dim;            // Disabled items (VGA dark gray).
    unsigned frame;          // Inactive frames (VGA light gray on blue).
    unsigned surface;        // Menus, dialogs, status line (VGA light gray).
    unsigned surfaceFg;      // Their text (VGA black).
    unsigned hot;            // Shortcut letters (VGA red).
    unsigned sel;            // Selected menu item, buttons (VGA green).
    unsigned accent;         // Cyan text.
    unsigned accentBg;       // Lists, check boxes (VGA cyan).
    unsigned shadow;         // Shadows (VGA black).
    unsigned termBg;         // Inside the session windows; 0: bg.
    unsigned ansi[16];       // Black, red, green, yellow, blue, magenta,
                             // cyan, white, then the bright ones.
};

extern const Theme themes[];
extern const size_t themeCount;

const Theme &currentTheme() noexcept;
size_t currentThemeIndex() noexcept;
// Selects a theme by id (unknown ids are ignored) or index, sets the session
// windows' colors and writes the Claude Code theme file. Returns whether it
// changed. The caller redraws.
bool selectTheme(const std::string &id) noexcept;
bool selectTheme(size_t index) noexcept;

// A text-mode attribute in the current theme's colors.
TColorAttr themeAttr(unsigned char bios) noexcept;

#endif // TVCLAUDE_THEME_H
