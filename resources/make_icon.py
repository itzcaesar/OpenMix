"""Draws the OpenMix icon: a dark channel faceplate with three faders, the middle cap lit in
the UI's lamp amber. Colours come from the dark theme (Source/ui/Theme.h).

    python resources/make_icon.py      # writes icon.png (1024 px) and icon.ico next to this file

TrayIcon.cpp draws the same shape in code, so keep the two in step.
"""
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).parent
SIZE = 1024
SS = 4  # supersampling for smooth edges

PANEL = (32, 32, 32, 255)        # Theme panel   #202020
EDGE = (58, 58, 58, 255)         # a little lighter than controlEdge, so the shape reads on dark taskbars
SLOT = (11, 11, 11, 255)         # meterWell     #0b0b0b
CAP = (230, 223, 204, 255)       # tape cream    #e6dfcc
AMBER = (240, 162, 59, 255)      # accent        #f0a23b
LINE = (26, 26, 26, 255)         # onAccent      #1a1a1a


def draw(size: int) -> Image.Image:
    s = size * SS / 1024.0
    img = Image.new('RGBA', (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def box(x0, y0, x1, y1):
        return [round(x0 * s), round(y0 * s), round(x1 * s), round(y1 * s)]

    # Faceplate
    d.rounded_rectangle(box(56, 56, 968, 968), radius=round(196 * s), fill=EDGE)
    d.rounded_rectangle(box(76, 76, 948, 948), radius=round(178 * s), fill=PANEL)

    # Three fader slots with caps at different heights (a mix in progress)
    for cx, cy, colour in ((300, 612, CAP), (512, 392, AMBER), (724, 520, CAP)):
        d.rounded_rectangle(box(cx - 30, 216, cx + 30, 808), radius=round(30 * s), fill=SLOT)
        d.rounded_rectangle(box(cx - 104, cy - 54, cx + 104, cy + 54), radius=round(22 * s), fill=colour)
        d.rectangle(box(cx - 104, cy - 7, cx + 104, cy + 7), fill=LINE)

    return img.resize((size, size), Image.LANCZOS)


if __name__ == '__main__':
    big = draw(1024)
    big.save(HERE / 'icon.png')
    # Small sizes are drawn directly (not scaled from 1024) so they stay crisp.
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [draw(n) for n in sizes]
    frames[-1].save(HERE / 'icon.ico', sizes=[(n, n) for n in sizes], append_images=frames[:-1])
    print('wrote', HERE / 'icon.png', 'and', HERE / 'icon.ico')
