#!/bin/sh
# Records demo/demo.gif: runs tvclaude with real Claude in two scratch
# folders, types demo/keys.sh into it, then draws the recording.
# Needs: script (util-linux), ffmpeg, python3 with pyte and pillow.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=${TMPDIR:-/tmp}/tvclaude-demo-rec
D=$HOME/tvclaude-demo
rm -rf "$work" "$D"; mkdir -p "$work/config" "$D/todo-app" "$D/blog"
printf 'def add(items, text):\n    items.append({"text": text, "done": False})\n' > "$D/todo-app/todo.py"
printf '# Todo app\n' > "$D/todo-app/README.md"
printf '# Hello world\n\nFirst post.\n' > "$D/blog/first-post.md"

bash "$here/keys.sh" | env -u CLAUDECODE -u CLAUDE_CODE_CHILD_SESSION -u CLAUDE_CODE_SESSION_ID \
    XDG_CONFIG_HOME="$work/config" TERM=foot COLORTERM=truecolor timeout 85 \
    script -qf --log-out "$work/out" --log-timing "$work/timing" \
    -c "stty cols 120 rows 34; $here/../build/tvclaude $D/todo-app $D/blog" >/dev/null 2>&1 || true

python3 "$here/cast2frames.py" "$work/out" "$work/timing" 120 34 "$work/frames"
cd "$work/frames"
ffmpeg -loglevel error -y -f concat -i list.ffconcat \
    -vf "split[a][b];[a]palettegen=max_colors=64:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    -fps_mode vfr "$here/demo.gif"
echo "wrote $here/demo.gif"
