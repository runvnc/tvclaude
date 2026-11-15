"""Runs a program in a pseudo-terminal, types scripted keys into it, and logs
its output with timings (the `script --log-timing` format cast2frames.py
reads). Output is read as soon as it is written, so a screen update is never
split in time the way piping keys into `script` can split it.

usage: recorder.py cols rows out.log timing.log keys.txt -- command...

keys.txt: one step per line, `sleep SECONDS`, `type TEXT` (typed one key at
a time), `key NAME` (enter, esc, f5, f6, alt-X, right), or `resize COLS ROWS`
(as a window manager resizing the terminal), # for comments.
"""
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time

cols, rows, out_path, tim_path, keys_path = sys.argv[1:6]
cols, rows = int(cols), int(rows)
cmd = sys.argv[sys.argv.index("--") + 1:]

KEYS = {"enter": b"\r", "esc": b"\x1b", "f5": b"\x1b[15~", "f6": b"\x1b[17~",
        "f7": b"\x1b[18~", "f8": b"\x1b[19~", "right": b"\x1b[C", "left": b"\x1b[D",
        "down": b"\x1b[B", "up": b"\x1b[A", "tab": b"\t", "ins": b"\x1b[2~"}

steps = []   # (delay before, bytes)
pending = 0.0
for line in open(keys_path):
    line = line.split("#", 1)[0].rstrip("\n").strip()
    if not line:
        continue
    word, _, arg = line.partition(" ")
    if word == "sleep":
        pending += float(arg)
    elif word == "type":
        for ch in arg:
            steps.append((pending, ch.encode()))
            pending = 0.045
    elif word == "key":
        name = arg.lower()
        if name.startswith("alt-"):
            data = b"\x1b" + name[4:].encode()
        else:
            data = KEYS[name]
        steps.append((pending, data))
        pending = 0.0
    elif word == "resize":
        c, r = arg.split()
        steps.append((pending, ("resize", int(c), int(r))))
        pending = 0.0
    else:
        sys.exit("bad step: " + line)
end_wait = pending

pid, fd = pty.fork()
if pid == 0:
    os.execvp(cmd[0], cmd)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

out = open(out_path, "wb")
tim = open(tim_path, "w")
start = last = time.monotonic()
deadline = start
step_i = 0
next_at = start + (steps[0][0] if steps else end_wait)
finish_at = None

while True:
    now = time.monotonic()
    if step_i < len(steps) and now >= next_at:
        data = steps[step_i][1]
        if isinstance(data, tuple):   # Resize: the kernel sends SIGWINCH.
            fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", data[2], data[1], 0, 0))
        else:
            os.write(fd, data)
        step_i += 1
        if step_i < len(steps):
            next_at = now + steps[step_i][0]
        else:
            finish_at = now + end_wait
    if finish_at is not None and now >= finish_at:
        break
    wake = next_at if step_i < len(steps) else finish_at
    r, _, _ = select.select([fd], [], [], max(0.0, min(wake - now, 0.05)))
    if r:
        try:
            data = os.read(fd, 65536)
        except OSError:
            break
        if not data:
            break
        now = time.monotonic()
        out.write(data)
        tim.write("%.6f %d\n" % (now - last, len(data)))
        last = now

os.kill(pid, 15)
out.close()
tim.close()
