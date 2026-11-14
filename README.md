# tvclaude

Claude Code sessions in Turbo Vision windows: a blue DOS-style desktop with a
menu bar, numbered overlapping windows and a status line, in the spirit of
Turbo Pascal and Turbo C++.

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
- **New Claude session** (F3) asks for a folder and can continue the last
  conversation there. **New shell** opens a plain terminal window.
- **Resume** (File > Resume) lists your 9 most recent Claude conversations
  by title, folder and age, from any project; **More sessions...** lists them
  all. Each opens in a new window with `claude --resume`, in its folder.
- **Turbo colors inside the windows**: yellow text on dark blue, a matching
  Claude Code theme, and your own prompts drawn black on teal.
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
| File | New Claude session (F3), Resume ▸, New shell, Export conversation, Change dir, DOS shell, Exit (Alt-X) |
| Edit | Undo (rewind), Copy reply, Select text, Paste |
| Session | Session list (Alt-0), Next needing you (F8), Rename, New conversation, Compact, Context, Review changes, New Claude session |
| Options | Model, Permissions, Agents, Config |
| Info | Usage, Stats |
| Workspace | New workspace, Rename, Close workspace, Next (F7), Previous (Shift-F7), one entry per workspace |
| Window | Tile, Tile rows, Cascade, Size/Move (Ctrl-F5), Zoom (F5), Next (F6), Previous (Shift-F6), Close (Alt-F3), Grab input |
| Help | About, Claude status |

The Claude items (Export, Undo, Copy reply, New conversation, Compact, Context,
Review changes, the Options and Info menus and Claude status) type their slash command, shown
beside each, into the Claude window in front.

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
./build/tvclaude                 # one Claude session in the current folder
./build/tvclaude ~/a ~/b ~/c     # one session per folder, tiled
```

To keep sessions running when the terminal closes:

```sh
dtach -A "$XDG_RUNTIME_DIR/tvclaude.sock" -r winch -z ./build/tvclaude
```

Run the same command again to reattach.

## How it works

- Each session runs `claude --settings '<json>' --plugin-dir plugin/`. The
  settings select the `tvclaude` theme and add hooks that write the session's
  state to a file under `$XDG_RUNTIME_DIR`; tvclaude polls those files.
  Nothing in your own Claude Code settings changes.
- On start, tvclaude writes the theme to `~/.claude/themes/tvclaude.json`
  (Claude Code reads themes only from there).
- `plugin/` is a small Claude Code plugin that draws your prompts black on
  teal while running inside tvclaude.
- Sessions see `TURBOVISION=1` in their environment.

## License

MIT, as tvterm. See [COPYRIGHT](COPYRIGHT).
