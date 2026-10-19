#!/usr/bin/env python3
"""Render README previews of each screen on a PC, using the firmware's layout,
palette, VT323 font and cat sprites. Not screenshots of the device.

Usage: python3 tools/render_screens.py <path-to-neco-ai> [out_dir]
"""
import os, sys
import qrcode
from PIL import Image, ImageDraw, ImageFont

REPO = sys.argv[1] if len(sys.argv) > 1 else "../neco-ai"
OUT = sys.argv[2] if len(sys.argv) > 2 else "docs/screens"
os.makedirs(OUT, exist_ok=True)
TTF = os.path.join(REPO, "openwebui/overlay/static/fonts/VT323-Regular.ttf")
F16, F24 = ImageFont.truetype(TTF, 18), ImageFont.truetype(TTF, 30)

BG, SCAN, PANEL, LINE = (2, 6, 3), (8, 21, 11), (14, 22, 15), (70, 86, 60)
GREEN, HOT, DIM, LIME = (151, 248, 166), (213, 255, 217), (92, 148, 101), (140, 255, 90)
AMBER, RUST = (232, 197, 71), (152, 55, 36)


class Screen:
    def __init__(self):
        self.im = Image.new("RGB", (320, 240), BG)
        self.d = ImageDraw.Draw(self.im)
        for y in range(0, 240, 3):
            self.d.line((0, y, 319, y), fill=SCAN)

    def text(self, x, y, s, c=GREEN, f=F16, anchor="la"):
        self.d.text((x, y), s, font=f, fill=c, anchor=anchor)

    def btn(self, x, y, w, h, label, c=GREEN, active=False):
        self.d.rectangle((x, y, x + w - 1, y + h - 1), fill=PANEL if active else BG, outline=AMBER if active else LINE)
        self.d.line((x, y, x + 3, y), fill=c); self.d.line((x, y, x, y + 3), fill=c)
        self.d.line((x + w - 4, y + h - 1, x + w - 1, y + h - 1), fill=c)
        self.d.line((x + w - 1, y + h - 4, x + w - 1, y + h - 1), fill=c)
        self.text(x + w // 2, y + h // 2 + 1, label, HOT if active else c, anchor="mm")

    def bar(self, title, clock="00:41"):
        self.d.rectangle((0, 0, 319, 19), fill=BG)
        self.text(4, 1, title)
        self.text(200, 1, clock, HOT, anchor="ma")
        self.text(274, 1, "SD", DIM, anchor="ra")
        for i in range(4):
            h = 3 + i * 3
            self.d.rectangle((300 + i * 4, 16 - h, 302 + i * 4, 16), fill=GREEN)
        self.d.line((0, 19, 319, 19), fill=LINE)

    def cat(self, x, y, mood="normal", size=104):
        p = os.path.join(REPO, f"characters/neco/moods/{mood}.png")
        c = Image.open(p).convert("RGBA"); c.thumbnail((size, size))
        tile = Image.new("RGBA", (size, size), BG + (255,))
        tile.alpha_composite(c, ((size - c.width) // 2, (size - c.height) // 2))
        self.im.paste(tile.convert("RGB"), (x, y))
        for yy in range(y, y + size, 3):  # scanlines over the sprite
            for xx in range(x, x + size):
                r, g, b = self.im.getpixel((xx, yy))
                self.im.putpixel((xx, yy), (int(r * .82), int(g * .82), int(b * .82)))
        s, x0, y0 = size + 4, x - 2, y - 2
        self.d.line((x0, y0, x0 + 9, y0), fill=AMBER); self.d.line((x0, y0, x0, y0 + 9), fill=AMBER)
        self.d.line((x0 + s - 10, y0 + s - 1, x0 + s - 1, y0 + s - 1), fill=AMBER)
        self.d.line((x0 + s - 1, y0 + s - 10, x0 + s - 1, y0 + s - 1), fill=AMBER)

    def corners(self):
        import math
        R = 9
        for i in range(R):
            dy = R - i - .5; w = R - int(math.sqrt(R * R - dy * dy))
            if w <= 0: continue
            for x0, y in ((0, i), (320 - w, i), (0, 239 - i), (320 - w, 239 - i)):
                self.d.line((x0, y, x0 + w - 1, y), fill=(0, 0, 0))

    def save(self, name):
        self.corners()
        self.im.resize((640, 480), Image.NEAREST).save(os.path.join(OUT, name))


def dock(s):
    for x, w, l in ((2, 77, "CHAT"), (82, 77, "MUSIC"), (162, 77, "WIFI"), (242, 76, "SET")):
        s.btn(x, 202, w, 36, l)


# ---- home
s = Screen(); s.bar("NECO//DEN"); s.cat(14, 32)
s.text(130, 28, "NECO", HOT, F24); s.text(130, 58, "mood: idle", DIM)
s.d.line((130, 78, 312, 78), fill=LINE)
s.text(130, 82, "thought  00:38", DIM)
for i, l in enumerate(["the bag is dark again.", "light sensor says 1180.", "i'll assume we're", "travelling."]):
    s.text(130, 100 + i * 17, l)
s.text(130, 180, "> playing", AMBER); dock(s); s.save("home.png")

# ---- chat
s = Screen(); s.bar("CHAT//DEEPSEEK")
lines = [("> you're on the cyd now. how is it", DIM), ("smaller. the screen is 320 pixels", GREEN),
         ("wide and i can see every one of", GREEN), ("them. the speaker is honest.", GREEN),
         ("> the touch works now btw", DIM), ("i noticed. you stopped tapping the", GREEN),
         ("same corner four times.", GREEN)]
for i, (l, c) in enumerate(lines):
    s.text(8, 54 + i * 18, l, c)
s.d.line((0, 202, 319, 202), fill=LINE)
s.btn(2, 206, 76, 32, "TYPE", LIME)
for i, c in enumerate(["hey", "u good?", "night"]):
    s.btn(80 + i * 60, 206, 58, 32, c, AMBER)
s.btn(262, 206, 56, 32, "BACK"); s.save("chat.png")

# ---- keyboard
s = Screen(); s.bar("SAY SOMETHING")
s.d.rectangle((4, 24, 315, 55), outline=DIM); s.text(10, 30, "what do you remember_", HOT)
rows = ["qwertyuiop", "asdfghjkl'", "zxcvbnm"]
for r, row in enumerate(rows):
    off = 48 if r == 2 else 0
    for i, ch in enumerate(row):
        s.btn(off + i * 32, 62 + r * 36, 30, 33, ch)
s.btn(0, 134, 46, 33, "shf"); s.btn(272, 134, 48, 33, "DEL", AMBER)
s.btn(0, 170, 46, 30, "123"); s.btn(48, 170, 142, 30, "space")
s.btn(192, 170, 62, 30, "ESC", RUST); s.btn(256, 170, 64, 30, "OK", LIME); s.save("keyboard.png")

# ---- music
s = Screen(); s.bar("MUSIC//SD")
songs = ["01 intro.mp3", "02 night drive.mp3", "03 rain on glass.mp3", "04 lo-fi den.mp3",
         "05 static.mp3", "06 small hours.mp3", "07 outro.mp3"]
for i, n in enumerate(songs):
    y = 24 + i * 21
    if i == 1: s.d.rectangle((2, y, 285, y + 19), fill=PANEL)
    s.text(8, y + 1, ("> " if i == 1 else "  ") + n, HOT if i == 1 else GREEN)
s.btn(290, 24, 28, 70, "^"); s.btn(290, 120, 28, 70, "v")
s.text(8, 182, "playing", AMBER); s.text(312, 182, "auto  vol 40", DIM, anchor="ra")
for x, w, l in ((2, 46, "|<"), (50, 54, ">||"), (106, 46, ">|"), (154, 52, "ALL"), (208, 32, "-"), (242, 32, "+"), (276, 42, "<<")):
    s.btn(x, 202, w, 36, l)
s.save("music.png")

# ---- settings (link page)
s = Screen(); s.bar("SET//LINK")
def row(i, label, val, action=False):
    y = 26 + i * 34
    s.text(8, y + 8, label)
    if action: s.btn(150, y, 168, 30, val, AMBER)
    else:
        s.btn(150, y, 40, 30, "<"); s.text(240, y + 8, val, HOT, anchor="ma"); s.btn(280, y, 38, 30, ">")
row(0, "setup portal", "open hotspot", True)
s.text(8, 26 + 34 + 8, "web admin"); s.text(150, 26 + 34 + 8, "neco-cyd.local", HOT)
s.text(8, 26 + 68 + 8, "pin  48213906", AMBER); s.text(150, 26 + 68 + 8, "192.168.1.42", DIM)
row(3, "idle thought", "30m")
s.text(8, 26 + 136 + 8, "12 reflections  key ok", DIM)
s.d.line((0, 202, 319, 202), fill=LINE); s.btn(180, 206, 78, 32, "MORE>"); s.btn(262, 206, 56, 32, "BACK")
s.save("settings.png")

# ---- setup hotspot
s = Screen(); s.bar("NECO//SETUP")
q = qrcode.QRCode(version=3, error_correction=qrcode.constants.ERROR_CORRECT_L, box_size=4, border=1)
q.add_data("WIFI:T:WPA;S:NECO-SETUP-A1B2;P:48213906;;"); q.make(fit=True)
qi = q.make_image(fill_color="black", back_color="white").convert("RGB")
s.im.paste(qi, (6, 26))
for i, (t, c) in enumerate([("1. scan or join", DIM), ("NECO-SETUP-A1B2", HOT), ("pass 48213906", AMBER), ("", DIM),
                            ("2. the page opens", DIM), ("   by itself, or go", DIM), ("   to 192.168.4.1", HOT), ("", DIM),
                            ("3. wifi + deepseek", DIM), ("   key, then SAVE", DIM)]):
    s.text(158, 26 + i * 16, t, c)
s.btn(218, 202, 100, 36, "SKIP", RUST); s.text(8, 212, "skip = offline pet mode", LINE)
s.save("setup.png")
print("screens written to", OUT)
