#!/usr/bin/env python3
import os
import math
from PIL import Image, ImageDraw, ImageFont

W = 1258
H = 240

# Normalized Plot Viewport Bounds
X_MIN = 48
X_MAX = 1150
Y_MIN = 30   # +15 dB
Y_MAX = 210  # -15 dB
Y_CENTER = 120 # 0 dB

# Base canvas
im = Image.new("RGBA", (W, H), (16, 18, 22, 255))
draw = ImageDraw.Draw(im)

# Outer card border
draw.rectangle([(0, 0), (W - 1, H - 1)], outline=(32, 36, 45, 255), width=1)

# Inner plot area viewport (recessed dark oscilloscope screen)
draw.rectangle([(X_MIN, Y_MIN), (X_MAX, Y_MAX)], fill=(12, 14, 18, 255), outline=(38, 45, 58, 255), width=1)

# Fonts
try:
    font_sm = ImageFont.truetype("fonts/Roboto-Regular.ttf", 11)
    font_bold = ImageFont.truetype("fonts/Roboto-Bold.ttf", 12)
except Exception:
    font_sm = font_bold = ImageFont.load_default()

# Frequency octave grid lines
freqs = [
    (30, "30"), (50, "50"), (100, "100"), (200, "200"),
    (500, "500"), (1000, "1k"), (2000, "2k"), (5000, "5k"),
    (10000, "10k"), (16000, "16k"), (20000, "20k")
]

def freq_to_x(f):
    t = math.log10(f / 20.0) / 3.0
    return int(round(X_MIN + t * (X_MAX - X_MIN)))

# Minor frequency lines (unlabeled)
minor_freqs = [
    40, 60, 70, 80, 90,
    300, 400,
    600, 700, 800, 900,
    3000, 4000,
    6000, 7000, 8000, 9000,
    12000, 14000, 18000
]
for f in minor_freqs:
    x = freq_to_x(f)
    if X_MIN < x < X_MAX:
        draw.line([(x, Y_MIN + 1), (x, Y_MAX - 1)], fill=(18, 22, 28, 255), width=1)

for f, label in freqs:
    if f < 20 or f > 20000:
        continue
    x = freq_to_x(f)
    if X_MIN < x < X_MAX:
        # Vertical grid line across the plot area
        draw.line([(x, Y_MIN + 1), (x, Y_MAX - 1)], fill=(25, 30, 40, 255), width=1)
    # Frequency label below the plot area
    draw.text((x, H - 14), label, fill=(75, 90, 110, 255), anchor="mm", font=font_sm)

# Horizontal decibel grid lines (+15, +10, +5, 0, -5, -10, -15 dB)
# y=120 is 0dB, scale: 6px per dB (+15dB at y=30, -15dB at y=210)
db_lines = [
    (+15, Y_MIN),
    (+10, 60),
    (+5, 90),
    (0, Y_CENTER),
    (-5, 150),
    (-10, 180),
    (-15, Y_MAX)
]

for db, y in db_lines:
    if db == 0:
        # 0dB baseline across the plot area
        draw.line([(X_MIN, y), (X_MAX, y)], fill=(45, 60, 80, 255), width=1)
        draw.text((X_MIN - 12, y - 1), "0 dB", fill=(110, 135, 165, 255), anchor="rm", font=font_bold)
    else:
        if Y_MIN < y < Y_MAX:
            draw.line([(X_MIN + 1, y), (X_MAX - 1, y)], fill=(22, 28, 38, 255), width=1)
        sign = "+" if db > 0 else ""
        draw.text((X_MIN - 12, y), f"{sign}{db}", fill=(65, 80, 100, 255), anchor="rm", font=font_sm)

# Title indicator top-left inside header
draw.text((X_MIN, 15), "5-BAND PARAMETRIC EQUALIZER", fill=(80, 100, 125, 255), anchor="lm", font=font_bold)

# Embed the 16 magic signature pixels at row 0 (x=0..15, y=0)
MAGIC_PIXELS = [
    (0x48, 0x4C, 0x4D, 0xFF), # 'H','L','M'
    (0x58, 0x5F, 0x45, 0xFF), # 'X','_','E'
    (0x51, 0x5F, 0x54, 0xFF), # 'Q','_','T'
    (0x45, 0x53, 0x54, 0xFF), # 'E','S','T'
    (0xDE, 0xAD, 0xBE, 0xFF),
    (0xEF, 0xFE, 0xED, 0xFF),
    (0x13, 0x37, 0x42, 0xFF),
    (0x73, 0x31, 0x24, 0xFF),
    (0xA5, 0x5A, 0xF0, 0xFF),
    (0x0F, 0xC3, 0x3C, 0xFF),
    (0x12, 0x34, 0x56, 0xFF),
    (0x78, 0x9A, 0xBC, 0xFF),
    (0xAA, 0xBB, 0xCC, 0xFF),
    (0xDD, 0xEE, 0xFF, 0xFF),
    (0x11, 0x22, 0x33, 0xFF),
    (0x44, 0x55, 0x66, 0xFF),
]

for x, rgba in enumerate(MAGIC_PIXELS):
    im.putpixel((x, 0), rgba)

out_dir = "images"
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "eq_canvas.png")
im.save(out_path, format="PNG")
print(f"Generated {out_path} ({W}x{H}) with normalized plot area [{X_MIN}..{X_MAX}, {Y_MIN}..{Y_MAX}]")

