"""Replay a `script --log-timing` recording through pyte and draw frames.

usage: cast2frames.py out.log timing.log cols rows outdir
Writes outdir/NNNNN.png and outdir/list.ffconcat (with durations).
"""
import os
import sys

import pyte
from PIL import Image, ImageDraw, ImageFont

log, tim, cols, rows, outdir = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
os.makedirs(outdir, exist_ok=True)

FONT = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf"
FONTB = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Bold.ttf"
FALLBACK = "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
SIZE = 15
font = ImageFont.truetype(FONT, SIZE)
fontb = ImageFont.truetype(FONTB, SIZE)
FALLBACKS = [ImageFont.truetype(p, SIZE) for p in (
    FALLBACK,
    "/usr/share/fonts/TTF/MesloLGMNerdFontMono-Regular.ttf",
    "/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
)]


def has_glyph(f, c):
    """False when the font would draw its missing-glyph box."""
    key = (id(f), c)
    if key not in _glyph_cache:
        try:
            m = f.getmask(c)
            notdef = f.getmask("\U000F0000")
            _glyph_cache[key] = bytes(m) != bytes(notdef) or m.size != notdef.size
        except Exception:
            _glyph_cache[key] = False
    return _glyph_cache[key]


_glyph_cache = {}


def font_for(c, bold):
    f = fontb if bold else font
    if has_glyph(f, c):
        return f
    for fb in FALLBACKS:
        if has_glyph(fb, c):
            return fb
    return f
asc, desc = font.getmetrics()
CW = round(font.getlength("M"))
CH = asc + desc + 1

VGA = {
    "black": (0, 0, 0), "red": (170, 0, 0), "green": (0, 170, 0), "brown": (170, 85, 0),
    "yellow": (170, 85, 0), "blue": (0, 0, 170), "magenta": (170, 0, 170), "cyan": (0, 170, 170),
    "white": (170, 170, 170), "brightblack": (85, 85, 85), "brightred": (255, 85, 85),
    "brightgreen": (85, 255, 85), "brightbrown": (255, 255, 85), "brightyellow": (255, 255, 85),
    "brightblue": (85, 85, 255), "brightmagenta": (255, 85, 255), "brightcyan": (85, 255, 255),
    "brightwhite": (255, 255, 255),
}
DEF_FG, DEF_BG = (255, 255, 255), (0, 0, 170)


def color(c, default):
    if c == "default":
        return default
    if c in VGA:
        return VGA[c]
    if len(c) == 6:
        try:
            return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))
        except ValueError:
            pass
    return default


# Box drawing: arms (left, right, up, down) with weight 1 (single) or 2 (double).
BOX = {}
def box(chars, l, r, u, d):
    for ch in chars:
        BOX[ch] = (l, r, u, d)
box("─", 1, 1, 0, 0); box("│", 0, 0, 1, 1); box("┌", 0, 1, 0, 1); box("┐", 1, 0, 0, 1)
box("└", 0, 1, 1, 0); box("┘", 1, 0, 1, 0); box("├", 0, 1, 1, 1); box("┤", 1, 0, 1, 1)
box("┬", 1, 1, 0, 1); box("┴", 1, 1, 1, 0); box("┼", 1, 1, 1, 1)
box("═", 2, 2, 0, 0); box("║", 0, 0, 2, 2); box("╔", 0, 2, 0, 2); box("╗", 2, 0, 0, 2)
box("╚", 0, 2, 2, 0); box("╝", 2, 0, 2, 0); box("╠", 0, 2, 2, 2); box("╣", 2, 0, 2, 2)
box("╦", 2, 2, 0, 2); box("╩", 2, 2, 2, 0); box("╬", 2, 2, 2, 2)
box("╟", 0, 1, 2, 2); box("╢", 1, 0, 2, 2); box("╤", 2, 2, 0, 1); box("╧", 2, 2, 1, 0)
box("╞", 0, 2, 1, 1); box("╡", 2, 0, 1, 1)

# Blocks: quadrant bits UL, UR, LL, LR.
QUAD = {"█": 15, "▀": 3, "▄": 12, "▌": 5, "▐": 10, "▘": 1, "▝": 2, "▖": 4, "▗": 8,
        "▛": 7, "▜": 11, "▙": 13, "▟": 14, "▚": 9, "▞": 6}
SHADE = {"░": 0.25, "▒": 0.5, "▓": 0.75}

LW = max(1, CW // 8)
G = max(2, CW // 4)


def draw_box(dr, x, y, arms, fg):
    cx, cy = x + CW // 2, y + CH // 2
    l, r, u, d = arms
    def hline(x0, x1, yy):
        dr.rectangle([min(x0, x1), yy, max(x0, x1), yy + LW - 1], fill=fg)
    def vline(y0, y1, xx):
        dr.rectangle([xx, min(y0, y1), xx + LW - 1, max(y0, y1)], fill=fg)
    vw = max(u, d)   # Weight of the vertical, for single arms meeting it.
    hw = max(l, r)
    for w, sgn, edge in ((l, -1, x), (r, 1, x + CW - 1)):
        if not w:
            continue
        offs = (-G, G) if w == 2 else (0,)
        for o in offs:
            if w == 2:
                if d and not u: t = o
                elif u and not d: t = -o
                elif u and d: t = G
                else: t = -G
            else:
                t = G if vw == 2 else 0
            hline(cx + sgn * t, edge, cy + o)
    for w, sgn, edge in ((u, -1, y), (d, 1, y + CH - 1)):
        if not w:
            continue
        offs = (-G, G) if w == 2 else (0,)
        for o in offs:
            if w == 2:
                if r and not l: t = o
                elif l and not r: t = -o
                elif l and r: t = G
                else: t = -G
            else:
                t = G if hw == 2 else 0
            vline(cy + sgn * t, edge, cx + o)


def render(screen):
    img = Image.new("RGB", (cols * CW, rows * CH), DEF_BG)
    dr = ImageDraw.Draw(img)
    for yy in range(rows):
        line = screen.buffer[yy]
        for xx in range(cols):
            ch = line[xx]
            fg, bg = color(ch.fg, DEF_FG), color(ch.bg, DEF_BG)
            if ch.reverse:
                fg, bg = bg, fg
            x, y = xx * CW, yy * CH
            dr.rectangle([x, y, x + CW - 1, y + CH - 1], fill=bg)
            c = ch.data
            if not c or c == " ":
                continue
            if c in BOX:
                draw_box(dr, x, y, BOX[c], fg)
            elif c in QUAD:
                q = QUAD[c]
                hw, hh = CW // 2, CH // 2
                for bit, (qx, qy, qw, qh) in ((1, (0, 0, hw, hh)), (2, (hw, 0, CW - hw, hh)),
                                              (4, (0, hh, hw, CH - hh)), (8, (hw, hh, CW - hw, CH - hh))):
                    if q & bit:
                        dr.rectangle([x + qx, y + qy, x + qx + qw - 1, y + qy + qh - 1], fill=fg)
            elif c in SHADE:
                a = SHADE[c]
                mix = tuple(round(bg[i] * (1 - a) + fg[i] * a) for i in range(3))
                dr.rectangle([x, y, x + CW - 1, y + CH - 1], fill=mix)
            else:
                f = font_for(c, ch.bold)
                if f in (font, fontb):
                    dr.text((x, y + 1), c, font=f, fill=fg)
                else:   # Centre a fallback glyph in its cell.
                    w = f.getlength(c)
                    dr.text((x + (CW - w) / 2, y + 1), c, font=f, fill=fg)
    return img


screen = pyte.Screen(cols, rows)
stream = pyte.ByteStream(screen)
data = open(log, "rb").read()
# Skip the `stty` echo: nothing of it is shown before tvclaude starts.
pos = 0
t = 0.0
frames = []   # (time, png path)
last_key = None
STEP = 0.1
next_sample = 0.0


def snapshot(at):
    global last_key
    key = tuple(
        tuple((c.data, c.fg, c.bg, c.reverse, c.bold) for c in (screen.buffer[y][x] for x in range(cols)))
        for y in range(rows))
    if key == last_key:
        return
    last_key = key
    path = os.path.join(outdir, "%05d.png" % len(frames))
    render(screen).save(path)
    frames.append((at, path))


for line in open(tim):
    delay, n = line.split()
    delay, n = float(delay), int(n)
    t += delay
    while next_sample < t:
        snapshot(next_sample)
        next_sample += STEP
    stream.feed(data[pos:pos + n])
    pos += n
snapshot(t)

# Durations: real time, with long pauses cut short.
with open(os.path.join(outdir, "list.ffconcat"), "w") as f:
    f.write("ffconcat version 1.0\n")
    for i, (at, path) in enumerate(frames):
        dur = (frames[i + 1][0] - at) if i + 1 < len(frames) else 4.0
        dur = min(dur, 1.6) if i + 1 < len(frames) else dur
        f.write("file '%s'\nduration %.2f\n" % (os.path.basename(path), dur))
    f.write("file '%s'\n" % os.path.basename(frames[-1][1]))
print(len(frames), "frames", cols * CW, "x", rows * CH)
