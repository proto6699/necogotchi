#!/usr/bin/env python3
"""Generate firmware headers from Neco's assets.

Usage: python3 tools/make_assets.py <path-to-neco-ai-repo>

Produces in src/assets/:
  font_vt323.h   VT323 as Adafruit-GFX fonts (VT16 small, VT24 big)
  sprites.h      cat mood sprites, RGB565, pre-blended on the Den background
  sounds.h       synthesized meow / purr / blip as 8-bit WAV
"""
import io, math, os, struct, sys, wave
from PIL import Image, ImageDraw, ImageFont

REPO = sys.argv[1] if len(sys.argv) > 1 else "../neco-ai"
OUT = os.path.join(os.path.dirname(__file__), "..", "src", "assets")
os.makedirs(OUT, exist_ok=True)
BG = (0x02, 0x06, 0x03)  # --ech0-bg from the Den


# ---------- font ----------
def gfx_font(ttf, px, name):
    font = ImageFont.truetype(ttf, px)
    ascent, descent = font.getmetrics()
    bitmaps, glyphs = bytearray(), []
    for code in range(0x20, 0x7F):
        ch = chr(code)
        adv = int(round(font.getlength(ch)))
        box = font.getbbox(ch)  # relative to top of ascent line
        w, h = max(0, box[2] - box[0]), max(0, box[3] - box[1])
        offset = len(bitmaps)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-box[0], -box[1]), ch, font=font, fill=255)
            bits, n = 0, 0
            for y in range(h):
                for x in range(w):
                    bits = (bits << 1) | (1 if img.getpixel((x, y)) > 110 else 0)
                    n += 1
                    if n == 8:
                        bitmaps.append(bits); bits = n = 0
            if n:
                bitmaps.append(bits << (8 - n))
        glyphs.append((offset, w, h, adv, box[0], box[1] - ascent))
    s = [f"const uint8_t {name}Bitmaps[] PROGMEM = {{"]
    s.append(",".join(f"0x{b:02X}" for b in bitmaps) + "};")
    s.append(f"const GFXglyph {name}Glyphs[] PROGMEM = {{")
    s.append(",\n".join("{%d,%d,%d,%d,%d,%d}" % g for g in glyphs) + "};")
    s.append(f"const GFXfont {name} PROGMEM = {{(uint8_t*){name}Bitmaps,(GFXglyph*){name}Glyphs,0x20,0x7E,{ascent + descent}}};")
    return "\n".join(s)


ttf = os.path.join(REPO, "openwebui/overlay/static/fonts/VT323-Regular.ttf")
with open(os.path.join(OUT, "font_vt323.h"), "w") as f:
    f.write("// VT323 by Peter Hull, SIL Open Font License 1.1 (see LICENSES/OFL-VT323.txt)\n#pragma once\n#include <TFT_eSPI.h>\n")
    f.write(gfx_font(ttf, 18, "VT16") + "\n")
    f.write(gfx_font(ttf, 30, "VT24") + "\n")


# ---------- sprites ----------
def sprite(path, size, name):
    im = Image.open(path).convert("RGBA")
    im.thumbnail((size, size), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), BG + (255,))
    canvas.alpha_composite(im, ((size - im.width) // 2, (size - im.height) // 2))
    px = canvas.convert("RGB").load()
    vals = []
    for y in range(size):
        for x in range(size):
            r, g, b = px[x, y]
            vals.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    body = ",".join(f"0x{v:04X}" for v in vals)
    return f"const uint16_t {name}[{size * size}] PROGMEM = {{{body}}};"


S = 104
cats = [  # (C name, file) — missing moods fall back to normal in firmware
    ("neco_normal", "characters/neco/moods/normal.png"),
    ("neco_sleep", "characters/neco/moods/sleep.png"),
    ("neco_yay", "characters/neco/moods/yay.png"),
    ("coneco_normal", "characters/coneco/moods/normal.png"),
    ("sakamoto_normal", "characters/sakamoto/moods/normal.png"),
    ("sakamoto_sleep", "characters/sakamoto/moods/sleep.png"),
]
with open(os.path.join(OUT, "sprites.h"), "w") as f:
    f.write(f"#pragma once\n#include <Arduino.h>\n#define CAT_SPRITE {S}\n")
    for name, rel in cats:
        f.write(sprite(os.path.join(REPO, rel), S, name) + "\n")


# ---------- sounds (synthesized, 8-bit unsigned mono WAV @ 16 kHz) ----------
RATE = 16000


def wav_bytes(samples):
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1); w.setsampwidth(1); w.setframerate(RATE)
    w.writeframes(bytes(max(0, min(255, int(128 + 120 * s))) for s in samples))
    w.close()
    return buf.getvalue()


def env(i, n, a=0.08, r=0.35):
    t = i / n
    return min(1, t / a) * (1 if t < 1 - r else max(0, (1 - t) / r))


def meow(dur=0.55):
    n = int(RATE * dur); out = []; ph = 0
    for i in range(n):
        t = i / n
        f0 = 520 + 380 * math.sin(math.pi * t) - 120 * t  # rise then fall: "mi-aow"
        ph += 2 * math.pi * f0 / RATE
        voice = sum(math.sin(ph * k) / k for k in range(1, 6))  # buzzy harmonics
        formant = 0.6 + 0.4 * math.sin(math.pi * t)            # "ee" -> "ow" opening
        out.append(0.45 * voice * formant * env(i, n))
    return out


def purr(dur=1.2):
    n = int(RATE * dur); out = []; ph = 0
    import random
    rnd = random.Random(6699)
    for i in range(n):
        t = i / RATE
        ph += 2 * math.pi * 55 / RATE
        am = 0.5 + 0.5 * math.sin(2 * math.pi * 24 * t)  # 24 Hz flutter
        out.append(0.55 * am * (0.6 * math.sin(ph) + 0.4 * rnd.uniform(-1, 1)) * env(i, n, 0.15, 0.3))
    return out


def blip(f=1320, dur=0.05):
    n = int(RATE * dur)
    return [0.5 * (1 if math.sin(2 * math.pi * f * i / RATE) > 0 else -1) * env(i, n, 0.05, 0.6) for i in range(n)]


def chirp():
    return blip(880, 0.05) + blip(1320, 0.06)


with open(os.path.join(OUT, "sounds.h"), "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n// synthesized, not samples\n")
    for name, data in [("snd_meow", meow()), ("snd_purr", purr()), ("snd_blip", blip()), ("snd_chirp", chirp())]:
        b = wav_bytes(data)
        f.write(f"const uint8_t {name}[{len(b)}] PROGMEM = {{" + ",".join(str(x) for x in b) + "};\n")
print("assets written to", os.path.abspath(OUT))


# ---------- persona prompts (from neco-ai characters/) ----------
def c_str(name, text):
    esc = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'const char {name}[] PROGMEM = "{esc}";\n'


with open(os.path.join(OUT, "prompts.h"), "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n// generated from neco-ai characters/; {name} and 'Echo' are substituted at runtime\n")
    f.write(c_str("P_VALUES", open(os.path.join(REPO, "characters/_shared/values.md")).read().strip()))
    for cat in ("neco", "coneco", "sakamoto"):
        for part in ("identity", "voice"):
            f.write(c_str(f"P_{cat.upper()}_{part.upper()}", open(os.path.join(REPO, f"characters/{cat}/{part}.md")).read().strip()))
print("prompts written")
