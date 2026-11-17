"""Draws the NppDev logo (the same design as docs/logo.svg) and writes installer/nppdev.ico,
the icon of NppDev-Setup.exe, plus docs/logo.png. Needs Pillow:  python make-icon.py"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
N = 1024                                   # drawn large, then scaled down for every icon size
S = N / 160                                # the SVG is 160 x 160


def p(x, y):
    return (x * S, y * S)


def gradient(size, a, b, diagonal):
    """A linear gradient from colour a to b: left to right, or top-left to bottom-right."""
    g = Image.new("RGB", (size, size))
    px = g.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * size - 2) if diagonal else x / (size - 1)
            px[x, y] = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
    return g


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def logo():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    # rounded square with a dark diagonal gradient and a thin border
    shape = Image.new("L", (N, N), 0)
    ImageDraw.Draw(shape).rounded_rectangle([p(4, 4), p(156, 156)], radius=34 * S, fill=255)
    img.paste(gradient(N, rgb("#1f2937"), rgb("#0d1117"), True), (0, 0), shape)
    ImageDraw.Draw(img).rounded_rectangle([p(4, 4), p(156, 156)], radius=34 * S, outline=rgb("#30363d"), width=round(4 * S))
    # < > in a green-to-blue gradient
    marks = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(marks)
    w = round(11 * S)
    for pts in ([p(58, 52), p(32, 80), p(58, 108)], [p(102, 52), p(128, 80), p(102, 108)]):
        d.line(pts, fill=255, width=w, joint="curve")
        for x, y in pts:                                    # round line ends
            d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)
    img.paste(gradient(N, rgb("#3fb950"), rgb("#58a6ff"), False), (0, 0), marks)
    # the slash and the cursor
    d = ImageDraw.Draw(img)
    for (a, b, colour, width) in ((p(90, 44), p(70, 116), "#e6edf3", 10), (p(74, 128), p(86, 128), "#3fb950", 6)):
        wd = round(width * S)
        d.line([a, b], fill=rgb(colour), width=wd)
        for x, y in (a, b):
            d.ellipse([x - wd / 2, y - wd / 2, x + wd / 2, y + wd / 2], fill=rgb(colour))
    return img


if __name__ == "__main__":
    big = logo()
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(HERE, "nppdev.ico"), sizes=[(s, s) for s in sizes])
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, "docs", "logo.png"))
    print("wrote installer/nppdev.ico and docs/logo.png")
