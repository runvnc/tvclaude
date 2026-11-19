# tvclaude

Claude Code sessions in Turbo Vision windows: a blue DOS-style desktop with a
menu bar, numbered overlapping windows and a status line, in the spirit of
Turbo Pascal and Turbo C++.

![Two Claude Code sessions working side by side in tvclaude](demo/demo.gif)

tvclaude is a fork of [tvterm](https://github.com/magiblot/tvterm) by
magiblot, a terminal emulator built on the modern
[Turbo Vision](https://github.com/magiblot/tvision) port. The original tvterm
README is in [README.tvterm.md](README.tvterm.md).

## Features

- **Several Claude sessions at once**, each in its own window. Tile, cascade,
  zoom (F5), next (F6), close (Alt-F3), and Alt-1…9 to jump to a window.
- **Session status at a glance.** Each session reports, through Claude Code
  hooks, whether it is working (●), needs you (!), is done (✓) or idle (○).
  The window title shows it, the menu bar counts it, the **Session List**
  (Alt-0) lists every session, and **F8** jumps to the next one that needs you.
- **Window names**: a Claude window is named after the folder Claude is
  working in, and follows it when Claude `cd`s elsewhere. Session > Rename
  gives a window your own name (leave it empty to go back to the folder);
  the name is saved with the desktop. After the name, the title shows what
  the conversation is about, as /resume lists it (its /rename title, else
  Claude's own title, else the last prompt).
- **Workspaces**: separate desktops of windows, e.g. one per project.
  Workspace > New workspace names one and can start a Claude session in its
  folder; F7 / Shift-F7 or the Workspace menu switch between them. Every
  session keeps running while hidden; the Session List and F8 reach sessions
  in any workspace, and the menu bar shows the workspace on screen.
- **Cascade that stays readable**: after Window > Cascade, bringing a window
  forward re-cascades in the new order, so every title bar stays in view.
  Moving or resizing a window by hand leaves cascade mode.
- **New windows follow your layout**: a new or resumed session is tiled or
  cascaded in with the others, whichever you picked last, instead of
  covering them. When tiled, closing a window tiles the rest again to fill
  its place. The choice is remembered between runs.
- **New Claude session** (F3) asks for a folder and can continue the last
  conversation there. **New shell** opens a plain terminal window.
- **Resume** (File > Resume) lists your 9 most recent Claude conversations
  by title, folder and age, from any project; **More sessions...** lists them
  all. Each opens in a new window with `claude --resume`, in its folder.
- **Themes** (Options > Theme): the classic Turbo Vision colors, a softer
  Turbo Vision that is easier on the eyes, and other retro looks (Norton
  Commander, Commodore 64, green and amber phosphor), or modern ones
  (Catppuccin Mocha and Latte, Tokyo Night, Dracula, Gruvbox, Nord, One
  Dark, Rose Pine, Solarized Dark and Light). A theme colors the whole
  desktop, the text inside the session windows and Claude Code itself, in
  running sessions too, and is remembered between runs.
- **The desktop comes back.** Like Turbo Pascal's `TURBO.DSK`, tvclaude
  saves every workspace and window (folder, place, number and Claude
  conversation) to
  `~/.config/tvclaude/desktop` every few seconds and on exit. Started with
  no folders, it reopens them, each resuming its conversation with
  `claude --resume`.
- **Detach and reattach** with [dtach](https://github.com/crigler/dtach): run
  the desktop under dtach, close the terminal, and every session keeps running.

## Menus

| Menu | Items |
| --- | --- |
| File | New Claude session (F3), Resume ▸, New shell, Attach file(s), Export conversation, Change dir, DOS shell, Detach, Exit (Alt-X) |
| Edit | Undo (rewind), Copy reply, Select text, Paste |
| Session | Session list (Alt-0), Next needing you (F8), Rename, New conversation, Compact, Context, Review changes, Background tasks, Memory, Init CLAUDE.md, Add folder, New Claude session |
| Options | Model, Effort, Fast mode, Permissions, Output style, MCP servers, Agents, Hooks, Plugins, Skills, Config, Theme ▸ (Retro ▸, Modern ▸), On exit |
| Info | Usage, Stats, Doctor, Release notes |
| Workspace | New workspace, Rename, Close workspace, Next (F7), Previous (Shift-F7), one entry per workspace |
| Window | Tile, Tile rows, Cascade, Size/Move (Ctrl-F5), Zoom (F5), Next (F6), Previous (Shift-F6), Close (Alt-F3), Grab input |
| Help | About, Claude status, Claude help, Send feedback |

The Claude items (Export, Undo, Copy reply, the Claude items of the Session
menu, the Options menu except Theme and On exit, the Info menu, and Claude
status, help and feedback) type their slash command (`/export`, `/effort`,
`/fast`, ...) into the Claude window in front.

F10 opens the menu bar. The desktop keeps these keys for itself; every other
key goes to the session in the front window.

## Building

```sh
git clone --recursive https://github.com/runvnc/tvclaude.git
cd tvclaude
cmake . -B ./build -DCMAKE_BUILD_TYPE=Release
cmake --build ./build
```

Requirements: CMake, a C++14 compiler, `libncursesw`, optionally `libgpm`
(Linux), Perl (for the bundled libvterm), and
[Claude Code](https://code.claude.com) on your `PATH`.

## Running

```sh
./build/tvclaude                 # the saved desktop, or one session here
./build/tvclaude ~/a ~/b ~/c     # one session per folder, tiled
```

**Keeping sessions running.** With [dtach](https://github.com/crigler/dtach)
installed (`pacman -S dtach`, `apt install dtach`, `brew install dtach`),
tvclaude starts itself under it. Closing the terminal, pressing `Ctrl-\`, or
File > Detach then only detaches: every session keeps running, and running
`tvclaude` again brings the desktop back (folders given then are ignored).
Exit (Alt-X) with sessions open asks whether to keep them running or quit
them all, and can remember the answer (Options > On exit). `--no-dtach` runs
tvclaude directly.

While tvclaude runs, starting it again reattaches rather than starting a new
build, so after rebuilding, quit for real first. Help > About shows the build
number (the commit count), commit and build date.

**A window of its own.** tvclaude draws exact DOS colors itself, but a
translucent or blurred terminal background shows through around it. A
launcher that opens an opaque [foot](https://codeberg.org/dnkl/foot) window
for it, here as a fish function:

```fish
function turbo
    foot -o colors-dark.alpha=1.0 -o colors-dark.blur=no \
        -o colors-dark.background=0000aa -o main.pad=0x0 \
        --working-directory=$PWD ~/tvclaude/build/tvclaude $argv &
    disown
end
```

(Older foot versions call the section `colors` instead of `colors-dark`.)

**Terminals.** tvclaude runs in any terminal; with `COLORTERM=truecolor`
it draws exact colors, otherwise the nearest of 256 or 16. Dropping a file
onto the window is up to the terminal, which types its path, and Claude
Code attaches pasted image and file paths:

| Terminal | Drag and drop |
|----------|---------------|
| kitty, WezTerm, Ghostty, Alacritty | pastes the path |
| GNOME Terminal, Konsole, iTerm2, Windows Terminal | pastes the path |
| foot | not supported |

(From each terminal's documentation; tried with tvclaude in kitty, where a
dropped screenshot attaches as an image.)

Where dropping does not work (foot, or a full-screen window), File > Attach
file(s) picks a file and pastes its path into the Claude window in front, the
same way. It opens in the folder you last attached from. Space or Ins marks
several files, even across folders, and Open attaches them all; dropping
several files at once works too.

## How it works

- Each session runs `claude --settings '<json>' --plugin-dir plugin/`. The
  settings select the `tvclaude` theme and add hooks that write the session's
  state to a file under `$XDG_RUNTIME_DIR`; tvclaude polls those files.
  Nothing in your own Claude Code settings changes.
- On start and on each theme change, tvclaude writes the matching Claude
  Code theme to `~/.claude/themes/tvclaude.json` (Claude Code reads themes
  only from there, and running sessions redraw when it changes). The chosen
  theme is saved in `~/.config/tvclaude/options`.
- `plugin/` is a small Claude Code plugin that draws your prompts in the
  theme's colors (black on teal in the classic one) while running inside
  tvclaude.
- Sessions see `TURBOVISION=1` in their environment.

## License

MIT, as tvterm. See [COPYRIGHT](COPYRIGHT).
