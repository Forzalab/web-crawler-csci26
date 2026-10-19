#!/usr/bin/env python3
"""ansi_html.py < frame.ansi > frame.html - a `tmux capture-pane -e` frame as one HTML page (for a screenshot).
SGR only: reset, bold, dim, italic, underline, reverse, 16/256/truecolor fg + bg. No dependencies.
ANSI_BG=#000000 sets the page / default background (e.g. a pure black projector check)."""
import html, os, re, sys

BASE16 = ["#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
          "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"]
FG, BG = "#d0d0d0", os.environ.get("ANSI_BG", "#101014")


def xterm(n):
    if n < 16:
        return BASE16[n]
    if n < 232:
        n -= 16
        v = [0, 95, 135, 175, 215, 255]
        return "#%02x%02x%02x" % (v[n // 36], v[n // 6 % 6], v[n % 6])
    g = 8 + (n - 232) * 10
    return "#%02x%02x%02x" % (g, g, g)


def sgr(params, st):
    p = [int(x) if x else 0 for x in params.split(";")] if params else [0]
    i = 0
    while i < len(p):
        c = p[i]
        if c == 0:
            st.clear()
        elif c in (1, 2, 3, 4, 7):
            st[c] = True
        elif c in (22, 23, 24, 27):
            st.pop({22: 1, 23: 3, 24: 4, 27: 7}[c], None)
            if c == 22:
                st.pop(2, None)
        elif 30 <= c <= 37 or 90 <= c <= 97:
            st["fg"] = BASE16[c - 30 if c < 90 else c - 82]
        elif 40 <= c <= 47 or 100 <= c <= 107:
            st["bg"] = BASE16[c - 40 if c < 100 else c - 92]
        elif c == 39:
            st.pop("fg", None)
        elif c == 49:
            st.pop("bg", None)
        elif c in (38, 48) and i + 1 < len(p):
            key = "fg" if c == 38 else "bg"
            if p[i + 1] == 5 and i + 2 < len(p):
                st[key] = xterm(p[i + 2])
                i += 2
            elif p[i + 1] == 2 and i + 4 < len(p):
                st[key] = "#%02x%02x%02x" % tuple(p[i + 2:i + 5])
                i += 4
        i += 1


def style(st):
    fg, bg = st.get("fg", FG), st.get("bg", BG)
    if st.get(7):
        fg, bg = bg, fg
    css = [f"color:{fg}"]
    if bg != BG:
        css.append(f"background:{bg}")
    if st.get(1):
        css.append("font-weight:bold")
    if st.get(2):
        css.append("opacity:.6")
    if st.get(3):
        css.append("font-style:italic")
    if st.get(4):
        css.append("text-decoration:underline")
    return ";".join(css)


def main():
    out, st = [], {}
    for line in sys.stdin.read().rstrip("\n").split("\n"):
        row = []
        for part in re.split(r"(\x1b\[[0-9;]*m)", line):
            if part.startswith("\x1b["):
                sgr(part[2:-1], st)
            elif part:
                row.append(f'<span style="{style(st)}">{html.escape(part)}</span>')
        out.append("".join(row))
    print('<!doctype html><meta charset="utf-8"><style>body{margin:0;background:%s}'
          'pre{margin:0;padding:8px;display:inline-block;font:14px/1.15 "DejaVu Sans Mono","Liberation Mono",monospace;'
          'color:%s;background:%s}</style><pre id="t">%s</pre>' % (BG, FG, BG, "\n".join(out)))


main()
