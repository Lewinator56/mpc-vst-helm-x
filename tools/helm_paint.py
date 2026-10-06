#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""helm_paint.py -- cosmetic paint postpass for Helm on MPC standalone.

Runs AFTER gen_vst.py to replace stock widget graphics with custom-styled,
flat Helm aesthetic elements (inspired by dragonfly VST's df_paint.py):
  1. Replaces sh_knob_r<R>.png filmstrips with custom Helm knobs (gunmetal body,
     amber active arc, white notch).
  2. Regenerates flat, un-beveled option selectors (sh_seg_*.png) and popup options
     (sh_popopt_*.png) with clean dark-slate backgrounds, crisp Helm cyan borders,
     and anti-aliased Titillium Web typography.
  3. Regenerates flat, custom toggle switch pills (sh_pill_on.png, sh_pill_off.png)
     with glowing cyan active thumbs.
  4. Regenerates flat buttons (sh_btn_*.png) and preset list tiles (sh_tile_*.png).
  5. Restyles live-text colours and focus rings in TUI.json.
"""
import json
import math
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VST_JSON = os.path.join(ROOT, "vst.json")
vendor = "Lewinator56"
plugin_name = "HelmX"
if os.path.exists(VST_JSON):
    try:
        with open(VST_JSON) as f:
            _v = json.load(f)
            vendor = _v.get("vendor", vendor)
            plugin_name = _v.get("name", plugin_name)
    except Exception:
        pass
SKIN_DIR = os.path.join(ROOT, "build", "skin", f"{vendor} - VST - {plugin_name}", "Plugin Skins")
TUI_PATH = os.path.join(SKIN_DIR, "TUI.json")

SS = 4                    # 4x supersampling for ultra-crisp antialiasing
FRAMES = 128              # must match shadow_skin.py FRAMES
MAX_IMAGE_H = 16384

# Helm Desktop Palette
CYAN_ACCENT = (32, 200, 255)
AMBER_ACCENT = (255, 150, 0)
WHITE = (240, 244, 250)
DIM_WHITE = (142, 149, 160)
DARK_BG = (24, 27, 32)
DARK_BORDER = (46, 52, 63)
ACTIVE_BG = (18, 42, 56)

# Fonts
FONT_REGULAR_PATH = os.path.join(ROOT, "tools", "html_art", "fonts", "TitilliumWeb-Regular.ttf")
FONT_SEMIBOLD_PATH = os.path.join(ROOT, "tools", "html_art", "fonts", "TitilliumWeb-SemiBold.ttf")
FONT_BOLD_PATH = os.path.join(ROOT, "tools", "html_art", "fonts", "TitilliumWeb-Bold.ttf")


def argb(c, a=255):
    return "%02x%02x%02x%02x" % (a, c[0], c[1], c[2])


def draw_rounded_rect(draw, box, radius, fill=None, outline=None, width=1):
    """Draw rounded rectangle with fallback if Pillow < 8.2."""
    if hasattr(draw, "rounded_rectangle"):
        draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)
    else:
        draw.rectangle(box, fill=fill, outline=outline, width=width)


def get_font(path, px):
    try:
        return ImageFont.truetype(path, int(round(px)))
    except Exception:
        return ImageFont.load_default()


def fit_font_size(text, max_w, font_path, start_px, min_px=9):
    """Find the best font size that fits within max_w, truncating with ellipsis if needed."""
    px = start_px
    while px >= min_px:
        f = get_font(font_path, px)
        w = f.getlength(text) if hasattr(f, "getlength") else f.getsize(text)[0]
        if w <= max_w:
            return f, text
        px -= 1
    # Truncate with ellipsis
    f = get_font(font_path, min_px)
    txt = text
    while len(txt) > 3:
        txt = txt[:-2].rstrip() + "\u2026"
        w = f.getlength(txt) if hasattr(f, "getlength") else f.getsize(txt)[0]
        if w <= max_w:
            return f, txt
    return f, text


# ---------------------------------------------------------------------------
# Knobs
# ---------------------------------------------------------------------------
def knob_frame(size, t, r=34):
    """Render one knob frame at value t (0..1) with Helm styling."""
    n = size * SS
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy = n / 2.0, n / 2.0
    rad = r * SS

    # Outer dark track ring
    d.ellipse((cx - rad, cy - rad, cx + rad, cy + rad),
              fill=(16, 18, 22, 255), outline=(36, 40, 48, 255), width=int(1.5 * SS))

    # Amber active arc (300 degree travel from -150 to +150 deg)
    start_angle = -150.0
    sweep = 300.0 * t
    if sweep > 1.0:
        arc_pts_outer = []
        arc_pts_inner = []
        steps = max(10, int(sweep / 3.0))
        for s in range(steps + 1):
            ang = math.radians(start_angle + (sweep * s / steps) - 90.0)
            arc_pts_outer.append((cx + math.cos(ang) * (rad - 1.5 * SS),
                                  cy + math.sin(ang) * (rad - 1.5 * SS)))
            arc_pts_inner.append((cx + math.cos(ang) * (rad - 4.5 * SS),
                                  cy + math.sin(ang) * (rad - 4.5 * SS)))
        poly = arc_pts_outer + arc_pts_inner[::-1]
        if len(poly) >= 3:
            d.polygon(poly, fill=AMBER_ACCENT + (255,))

    # Inner knob face
    in_rad = rad - 6 * SS
    d.ellipse((cx - in_rad, cy - in_rad, cx + in_rad, cy + in_rad),
              fill=(24, 26, 32, 255), outline=(44, 48, 58, 255), width=int(1.5 * SS))

    # White notch pointer
    curr_ang = math.radians(start_angle + sweep - 90.0)
    p0 = (cx + math.cos(curr_ang) * (in_rad * 0.45),
          cy + math.sin(curr_ang) * (in_rad * 0.45))
    p1 = (cx + math.cos(curr_ang) * (in_rad * 0.92),
          cy + math.sin(curr_ang) * (in_rad * 0.92))
    d.line([p0, p1], fill=(255, 255, 255, 255), width=int(3.0 * SS))

    return im.resize((size, size), Image.LANCZOS)


def generate_knob_strip(size, r, path, frames):
    """Generate a full filmstrip PNG for a knob of given radius."""
    out = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    for k in range(frames):
        t = k / float(frames - 1)
        out.paste(knob_frame(size, t, r), (0, k * size))
    out.save(path, optimize=True)


# ---------------------------------------------------------------------------
# Sliders and Step Fader Bars
# ---------------------------------------------------------------------------
def draw_step_fader_frame(w, h, t, bipolar=True):
    """Draw a single frame of a draggable vertical step sequencer level bar (Helm aesthetics)."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad_y = 2.0 * SS

    # Dark background slate
    d.rectangle([0, pad_y, w_ss - 1, h_ss - pad_y], fill=(16, 18, 23, 255))
    # Border dividers: vertical right edge and horizontal top/bottom
    d.line([(w_ss - 1, pad_y), (w_ss - 1, h_ss - pad_y)], fill=(32, 38, 48, 255), width=int(1.0 * SS))
    d.line([(0, pad_y), (w_ss - 1, pad_y)], fill=(32, 38, 48, 255), width=int(1.0 * SS))
    d.line([(0, h_ss - pad_y), (w_ss - 1, h_ss - pad_y)], fill=(32, 38, 48, 255), width=int(1.0 * SS))

    y_mid = h_ss / 2.0
    margin_y = 6.0 * SS
    cap_h = 7.0 * SS
    travel = h_ss - 2 * pad_y - 2 * margin_y
    y_cap = pad_y + margin_y + (1.0 - t) * travel

    if bipolar:
        # Subtle 25% and 75% reference lines
        for pct in (0.25, 0.75):
            gy = pad_y + pct * (h_ss - 2 * pad_y)
            d.line([(3 * SS, gy), (w_ss - 4 * SS, gy)], fill=(24, 28, 36, 255), width=int(1.0 * SS))

        # Center zero reference line
        d.line([(2 * SS, y_mid), (w_ss - 3 * SS, y_mid)], fill=(48, 56, 70, 255), width=int(1.2 * SS))

        # Active fill from center
        if t > 0.51:
            top_y = y_cap
            bot_y = y_mid
            d.rectangle([3 * SS, top_y, w_ss - 4 * SS, bot_y], fill=(14, 52, 70, 255))
            d.rectangle([5 * SS, top_y, w_ss - 6 * SS, bot_y], fill=(18, 95, 125, 255))
        elif t < 0.49:
            top_y = y_mid
            bot_y = y_cap
            d.rectangle([3 * SS, top_y, w_ss - 4 * SS, bot_y], fill=(14, 52, 70, 255))
            d.rectangle([5 * SS, top_y, w_ss - 6 * SS, bot_y], fill=(18, 95, 125, 255))

        # Draggable cap
        is_active = abs(t - 0.5) > 0.02
        c_fill = CYAN_ACCENT + (255,) if is_active else (65, 78, 94, 255)
        c_glow = CYAN_ACCENT + (70,) if is_active else (40, 50, 62, 50)
        c_line = (255, 255, 255, 255) if is_active else (150, 175, 195, 255)

        cap_box = [2 * SS, y_cap - cap_h / 2.0, w_ss - 3 * SS, y_cap + cap_h / 2.0]
        # Halo Glow
        d.rectangle([1 * SS, y_cap - cap_h / 2.0 - 1.5 * SS, w_ss - 2 * SS, y_cap + cap_h / 2.0 + 1.5 * SS], fill=c_glow)
        draw_rounded_rect(d, cap_box, radius=2.0 * SS, fill=c_fill, outline=(255, 255, 255, 180), width=int(1.0 * SS))
        # Center hot line on cap
        d.line([(5 * SS, y_cap), (w_ss - 6 * SS, y_cap)], fill=c_line, width=int(1.5 * SS))
    else:
        # Unipolar fill from bottom up
        bot_y = pad_y + margin_y + travel
        if t > 0.01:
            d.rectangle([3 * SS, y_cap, w_ss - 4 * SS, bot_y], fill=(14, 52, 70, 255))
            d.rectangle([5 * SS, y_cap, w_ss - 6 * SS, bot_y], fill=(18, 95, 125, 255))
        c_fill = CYAN_ACCENT + (255,) if t > 0.01 else (65, 78, 94, 255)
        c_glow = CYAN_ACCENT + (70,) if t > 0.01 else (40, 50, 62, 50)
        c_line = (255, 255, 255, 255) if t > 0.01 else (150, 175, 195, 255)
        cap_box = [2 * SS, y_cap - cap_h / 2.0, w_ss - 3 * SS, y_cap + cap_h / 2.0]
        d.rectangle([1 * SS, y_cap - cap_h / 2.0 - 1.5 * SS, w_ss - 2 * SS, y_cap + cap_h / 2.0 + 1.5 * SS], fill=c_glow)
        draw_rounded_rect(d, cap_box, radius=2.0 * SS, fill=c_fill, outline=(255, 255, 255, 180), width=int(1.0 * SS))
        d.line([(5 * SS, y_cap), (w_ss - 6 * SS, y_cap)], fill=c_line, width=int(1.5 * SS))

    return im.resize((w, h), Image.LANCZOS)


def draw_slider_v_frame(w, h, t):
    """Draw a single frame of a general vertical fader with track and cap."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad_y = 4.0 * SS
    track_w = max(6.0 * SS, min(14.0 * SS, w_ss * 0.22))
    tx0 = (w_ss - track_w) / 2.0
    tx1 = tx0 + track_w
    ty0 = pad_y
    ty1 = h_ss - pad_y

    draw_rounded_rect(d, [tx0, ty0, tx1, ty1], radius=track_w / 2.0, fill=(14, 16, 20, 255), outline=(36, 42, 52, 255), width=int(1.2 * SS))

    cx = w_ss / 2.0
    d.line([(cx, ty0 + 3 * SS), (cx, ty1 - 3 * SS)], fill=(24, 28, 34, 255), width=int(1.5 * SS))

    cap_h = max(18.0 * SS, min(36.0 * SS, h_ss * 0.18))
    cap_w = max(w_ss - 4.0 * SS, track_w + 10.0 * SS)
    cap_y = ty0 + (1.0 - t) * (ty1 - ty0 - cap_h)

    if t > 0.02:
        draw_rounded_rect(d, [tx0 + 1.5 * SS, cap_y + cap_h / 2.0, tx1 - 1.5 * SS, ty1 - 1.5 * SS],
                          radius=2.0 * SS, fill=(18, 60, 80, 255))

    cx0 = (w_ss - cap_w) / 2.0
    cx1 = cx0 + cap_w
    cy0 = cap_y
    cy1 = cap_y + cap_h

    draw_rounded_rect(d, [cx0, cy0, cx1, cy1], radius=3.0 * SS, fill=(38, 44, 54, 255), outline=(56, 64, 78, 255), width=int(1.2 * SS))
    draw_rounded_rect(d, [cx0 + 1.5 * SS, cy0 + 1.5 * SS, cx1 - 1.5 * SS, cy1 - 1.5 * SS],
                      radius=2.5 * SS, fill=(28, 32, 40, 255))
    notch_h = 2.0 * SS
    notch_y = cy0 + (cap_h - notch_h) / 2.0
    d.rectangle([cx0 + 3 * SS, notch_y, cx1 - 3 * SS, notch_y + notch_h], fill=CYAN_ACCENT + (255,))

    return im.resize((w, h), Image.LANCZOS)


def draw_slider_h_frame(w, h, t):
    """Draw a single frame of a horizontal slider, padded into sq x sq."""
    sq = max(w, h)
    sq_ss = sq * SS
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (sq_ss, sq_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    off_x = (sq_ss - w_ss) / 2.0
    off_y = (sq_ss - h_ss) / 2.0

    pad_x = 4.0 * SS
    track_h = max(6.0 * SS, min(14.0 * SS, h_ss * 0.22))
    tx0 = off_x + pad_x
    tx1 = off_x + w_ss - pad_x
    ty0 = off_y + (h_ss - track_h) / 2.0
    ty1 = ty0 + track_h

    draw_rounded_rect(d, [tx0, ty0, tx1, ty1], radius=track_h / 2.0, fill=(14, 16, 20, 255), outline=(36, 42, 52, 255), width=int(1.2 * SS))

    cy = off_y + h_ss / 2.0
    d.line([(tx0 + 3 * SS, cy), (tx1 - 3 * SS, cy)], fill=(24, 28, 34, 255), width=int(1.5 * SS))

    cap_w = max(18.0 * SS, min(36.0 * SS, w_ss * 0.18))
    cap_h = max(h_ss - 4.0 * SS, track_h + 10.0 * SS)
    cap_x = tx0 + t * (tx1 - tx0 - cap_w)
    cx0 = cap_x
    cx1 = cap_x + cap_w
    cy0 = off_y + (h_ss - cap_h) / 2.0
    cy1 = cy0 + cap_h

    draw_rounded_rect(d, [cx0, cy0, cx1, cy1], radius=3.0 * SS, fill=(38, 44, 54, 255), outline=(56, 64, 78, 255), width=int(1.2 * SS))
    draw_rounded_rect(d, [cx0 + 1.5 * SS, cy0 + 1.5 * SS, cx1 - 1.5 * SS, cy1 - 1.5 * SS],
                      radius=2.5 * SS, fill=(28, 32, 40, 255))
    notch_w = 2.0 * SS
    notch_x = cx0 + (cap_w - notch_w) / 2.0
    d.rectangle([notch_x, cy0 + 3 * SS, notch_x + notch_w, cy1 - 3 * SS], fill=CYAN_ACCENT + (255,))

    return im.resize((sq, sq), Image.LANCZOS)


def generate_slider_strip(w, h, orient, path, frames, is_step=True):
    """Generate a full filmstrip PNG for a vertical or horizontal slider."""
    if orient == "v":
        out = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
        for k in range(frames):
            t = k / float(frames - 1)
            frame = draw_step_fader_frame(w, h, t) if is_step else draw_slider_v_frame(w, h, t)
            out.paste(frame, (0, k * h))
        out.save(path, optimize=True)
    else:
        sq = max(w, h)
        out = Image.new("RGBA", (sq, sq * frames), (0, 0, 0, 0))
        for k in range(frames):
            t = k / float(frames - 1)
            out.paste(draw_slider_h_frame(w, h, t), (0, k * sq))
        out.save(path, optimize=True)
def draw_segment(w, h, text, on):
    """Draw a flat, sleek Helm-styled option segment button (zero bevels)."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad = 1.0 * SS
    r = 4.0 * SS
    box = [pad, pad, w_ss - pad, h_ss - pad]

    if not on:
        # Inactive: Dark gunmetal slate with subtle border and dim-white text
        draw_rounded_rect(d, box, radius=r, fill=DARK_BG + (255,), outline=DARK_BORDER + (255,), width=int(1.2 * SS))
        max_tw = w_ss - 12 * SS
        f, txt = fit_font_size(text, max_tw, FONT_SEMIBOLD_PATH, int(h * 0.44 * SS))
        cx = w_ss / 2.0
        cy = h_ss / 2.0
        d.text((cx, cy), txt, font=f, fill=DIM_WHITE + (255,), anchor="mm")
    else:
        # Active: Rich dark cyan background, Helm cyan border, accent pill, crisp bright text
        draw_rounded_rect(d, box, radius=r, fill=ACTIVE_BG + (255,), outline=CYAN_ACCENT + (255,), width=int(2.0 * SS))

        # Glowing accent pill indicator on left edge
        bar_x0 = 3.5 * SS
        bar_x1 = 7.5 * SS
        bar_y0 = 5.0 * SS
        bar_y1 = h_ss - 5.0 * SS
        draw_rounded_rect(d, [bar_x0, bar_y0, bar_x1, bar_y1], radius=2.0 * SS, fill=CYAN_ACCENT + (255,))

        max_tw = w_ss - (bar_x1 + 8 * SS)
        f, txt = fit_font_size(text, max_tw, FONT_BOLD_PATH, int(h * 0.44 * SS))
        cx = (bar_x1 + w_ss) / 2.0
        cy = h_ss / 2.0
        d.text((cx, cy), txt, font=f, fill=(255, 255, 255, 255), anchor="mm")

    return im.resize((w, h), Image.LANCZOS)


def draw_toggle_pill(w, h, on):
    """Draw a flat, sleek Helm-styled toggle switch pill (zero bevels)."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad = 1.5 * SS
    box = [pad, pad, w_ss - pad, h_ss - pad]
    r_track = (h_ss - 2 * pad) / 2.0

    if not on:
        # Off: Recessed dark track, slate thumb on the left
        track_fill = (13, 16, 20, 255)
        track_border = (38, 44, 54, 255)
        draw_rounded_rect(d, box, radius=r_track, fill=track_fill, outline=track_border, width=int(1.2 * SS))

        cx = 14.0 * SS
        cy = h_ss / 2.0
        rad = 8.5 * SS
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(54, 62, 74, 255), outline=(24, 28, 34, 255), width=int(1.2 * SS))
    else:
        # On: Cyan glowing track, vibrant cyan thumb on the right
        track_fill = (12, 38, 52, 255)
        track_border = CYAN_ACCENT + (255,)
        draw_rounded_rect(d, box, radius=r_track, fill=track_fill, outline=track_border, width=int(1.5 * SS))

        cx = w_ss - 14.0 * SS
        cy = h_ss / 2.0
        rad = 8.5 * SS
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=CYAN_ACCENT + (255,), outline=(255, 255, 255, 255), width=int(1.5 * SS))

    return im.resize((w, h), Image.LANCZOS)


def draw_power_button(size, on):
    """Draw a round power on/off button of given size (e.g. 40x40) with illuminated glowing icon."""
    n = size * SS
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    cx, cy = n / 2.0, n / 2.0
    pad = 1.5 * SS
    r_btn = (n - 2 * pad) / 2.0

    if not on:
        # OFF: Dark circular button body with dark border
        d.ellipse([cx - r_btn, cy - r_btn, cx + r_btn, cy + r_btn],
                  fill=(22, 25, 30, 255), outline=DARK_BORDER + (255,), width=int(1.2 * SS))

        # Subtle inner bevel ring
        r_inner = r_btn - 2.5 * SS
        d.ellipse([cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
                  fill=(18, 20, 24, 255))

        # Unlit Power Icon: circle arc + top vertical line
        icon_r = r_btn * 0.48
        arc_box = [cx - icon_r, cy - icon_r + 1.5 * SS, cx + icon_r, cy + icon_r + 1.5 * SS]
        d.arc(arc_box, start=305, end=235, fill=DIM_WHITE + (255,), width=int(2.2 * SS))

        # Top vertical line
        line_y0 = cy - icon_r * 0.95
        line_y1 = cy + icon_r * 0.2
        d.line([(cx, line_y0), (cx, line_y1)], fill=DIM_WHITE + (255,), width=int(2.4 * SS))

    else:
        # ON: Glowing illuminated cyan button
        d.ellipse([cx - r_btn, cy - r_btn, cx + r_btn, cy + r_btn],
                  fill=ACTIVE_BG + (255,), outline=CYAN_ACCENT + (255,), width=int(1.8 * SS))

        # Subtle inner glow
        r_inner = r_btn - 2.5 * SS
        d.ellipse([cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
                  fill=(12, 44, 62, 255))

        icon_r = r_btn * 0.48
        arc_box = [cx - icon_r, cy - icon_r + 1.5 * SS, cx + icon_r, cy + icon_r + 1.5 * SS]

        # Halo bloom around arc
        d.arc(arc_box, start=305, end=235, fill=CYAN_ACCENT + (80,), width=int(4.5 * SS))
        # Main cyan arc
        d.arc(arc_box, start=305, end=235, fill=CYAN_ACCENT + (255,), width=int(2.4 * SS))
        # Hot white core
        d.arc(arc_box, start=305, end=235, fill=WHITE + (230,), width=int(1.0 * SS))

        # Top vertical line
        line_y0 = cy - icon_r * 0.95
        line_y1 = cy + icon_r * 0.2
        # Line halo
        d.line([(cx, line_y0), (cx, line_y1)], fill=CYAN_ACCENT + (80,), width=int(4.5 * SS))
        # Main cyan line
        d.line([(cx, line_y0), (cx, line_y1)], fill=CYAN_ACCENT + (255,), width=int(2.4 * SS))
        # Hot white line core
        d.line([(cx, line_y0), (cx, line_y1)], fill=WHITE + (230,), width=int(1.0 * SS))

    return im.resize((size, size), Image.LANCZOS)



def draw_glowing_switch(w, h, text, on):
    """Draw a larger illuminated glowing on-switch with an integrated LED indicator and text."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad = 1.2 * SS
    r = 5.0 * SS
    box = [pad, pad, w_ss - pad, h_ss - pad]

    led_x = 16.0 * SS
    led_y = h_ss / 2.0

    if not on:
        draw_rounded_rect(d, box, radius=r, fill=DARK_BG + (255,), outline=DARK_BORDER + (255,), width=int(1.2 * SS))
        # Unlit dark LED indicator
        led_r = 4.5 * SS
        d.ellipse([led_x - led_r, led_y - led_r, led_x + led_r, led_y + led_r], fill=(42, 48, 58, 255), outline=(20, 24, 30, 255), width=int(1.0 * SS))
        max_tw = w_ss - (led_x + led_r + 14 * SS)
        f, txt = fit_font_size(text, max_tw, FONT_SEMIBOLD_PATH, int(h * 0.42 * SS))
        cx = (led_x + led_r + w_ss) / 2.0
        d.text((cx, led_y), txt, font=f, fill=DIM_WHITE + (255,), anchor="mm")
    else:
        # ON: Rich dark cyan illuminated body, glowing cyan border, bright LED with hot core, crisp bright text
        draw_rounded_rect(d, box, radius=r, fill=ACTIVE_BG + (255,), outline=CYAN_ACCENT + (255,), width=int(2.0 * SS))
        # Outer halo glow around LED
        glow_r = 8.5 * SS
        d.ellipse([led_x - glow_r, led_y - glow_r, led_x + glow_r, led_y + glow_r], fill=CYAN_ACCENT + (70,))
        # Vibrant illuminated cyan LED
        led_r = 4.5 * SS
        d.ellipse([led_x - led_r, led_y - led_r, led_x + led_r, led_y + led_r], fill=CYAN_ACCENT + (255,), outline=(255, 255, 255, 200), width=int(1.0 * SS))
        # Hot white center core
        core_r = 2.0 * SS
        d.ellipse([led_x - core_r, led_y - core_r, led_x + core_r, led_y + core_r], fill=(255, 255, 255, 255))
        max_tw = w_ss - (led_x + led_r + 14 * SS)
        f, txt = fit_font_size(text, max_tw, FONT_BOLD_PATH, int(h * 0.42 * SS))
        cx = (led_x + led_r + w_ss) / 2.0
        d.text((cx, led_y), txt, font=f, fill=(255, 255, 255, 255), anchor="mm")

    return im.resize((w, h), Image.LANCZOS)


def draw_large_toggle(w, h, on):
    """Draw a larger glowing toggle pill (e.g. 68x34)."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    pad = 1.5 * SS
    box = [pad, pad, w_ss - pad, h_ss - pad]
    r_track = (h_ss - 2 * pad) / 2.0

    if not on:
        track_fill = (13, 16, 20, 255)
        track_border = (42, 48, 58, 255)
        draw_rounded_rect(d, box, radius=r_track, fill=track_fill, outline=track_border, width=int(1.5 * SS))
        cx = 17.0 * SS
        cy = h_ss / 2.0
        rad = 11.0 * SS
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(58, 66, 78, 255), outline=(26, 30, 36, 255), width=int(1.2 * SS))
    else:
        track_fill = (12, 40, 56, 255)
        track_border = CYAN_ACCENT + (255,)
        draw_rounded_rect(d, box, radius=r_track, fill=track_fill, outline=track_border, width=int(2.0 * SS))
        cx = w_ss - 17.0 * SS
        cy = h_ss / 2.0
        glow_rad = 15.0 * SS
        d.ellipse([cx - glow_rad, cy - glow_rad, cx + glow_rad, cy + glow_rad], fill=CYAN_ACCENT + (50,))
        rad = 11.0 * SS
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=CYAN_ACCENT + (255,), outline=(255, 255, 255, 255), width=int(1.8 * SS))
        core_rad = 4.0 * SS
        d.ellipse([cx - core_rad, cy - core_rad, cx + core_rad, cy + core_rad], fill=(255, 255, 255, 230))

    return im.resize((w, h), Image.LANCZOS)


def draw_tile(w, h, on):
    """Draw flat list tile background for preset/bank lists."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = 1.0 * SS
    r = 4.0 * SS
    box = [pad, pad, w_ss - pad, h_ss - pad]

    if not on:
        draw_rounded_rect(d, box, radius=r, fill=(15, 17, 21, 255), outline=(32, 36, 44, 255), width=int(1.0 * SS))
    else:
        draw_rounded_rect(d, box, radius=r, fill=ACTIVE_BG + (255,), outline=CYAN_ACCENT + (255,), width=int(2.0 * SS))

    return im.resize((w, h), Image.LANCZOS)


def draw_arrow(w, h, side):
    """Draw crisp cyan directional arrow."""
    w_ss, h_ss = w * SS, h * SS
    im = Image.new("RGBA", (w_ss, h_ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy = w_ss / 2.0, h_ss / 2.0
    s = min(w_ss, h_ss) * 0.35

    if side == "prev":
        pts = [(cx + s * 0.7, cy - s), (cx - s * 0.7, cy), (cx + s * 0.7, cy + s)]
    else:
        pts = [(cx - s * 0.7, cy - s), (cx + s * 0.7, cy), (cx - s * 0.7, cy + s)]
    d.polygon(pts, fill=CYAN_ACCENT + (255,))
    return im.resize((w, h), Image.LANCZOS)


def build_options_map():
    """Extract option labels for all enum/popup parameters from params.json and layout.conf,
    plus in-section switch labels from layout.conf."""
    options_map = {}
    switch_labels = {}
    params_path = os.path.join(ROOT, "params.json")
    if os.path.exists(params_path):
        try:
            with open(params_path) as f:
                pdata = json.load(f)
                for p in pdata.get("params", []):
                    if "options" in p and p.get("key"):
                        options_map[p["key"]] = p["options"]
        except Exception as e:
            print("  Warning: could not read params.json:", e)

    layout_path = os.path.join(ROOT, "layout.conf")
    if os.path.exists(layout_path):
        try:
            with open(layout_path) as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    m_key = re.search(r'\bkey=([a-zA-Z0-9_]+)', line)
                    m_opt = re.search(r'\boptions="([^"]+)"', line)
                    if m_key and m_opt:
                        k = m_key.group(1)
                        opts = [o.strip() for o in m_opt.group(1).split(",")]
                        options_map[k] = opts
                    m_lab = re.search(r'\blabel="([^"]*)"', line)
                    if line.startswith("toggle ") and m_key and m_lab:
                        k = m_key.group(1)
                        l = m_lab.group(1).strip()
                        if l:
                            switch_labels[k] = l
        except Exception as e:
            print("  Warning: could not read layout.conf:", e)

    return options_map, switch_labels


# ---------------------------------------------------------------------------
# Main Paint Routine
# ---------------------------------------------------------------------------
def paint_helm():
    if not os.path.exists(TUI_PATH):
        print("TUI.json not found at %s, skipping paint pass." % TUI_PATH)
        return

    tui = json.load(open(TUI_PATH))
    L = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    defs = {x["key"]: x["value"] for x in L}

    # 1. Knob filmstrips
    radii_used = {}
    for k, v in defs.items():
        if not k.startswith("shKnob"):
            continue
        for sub in v.get("componentsData", []):
            cd = sub["componentData"]
            if cd.get("type") == "Knob" and cd.get("data", {}).get("knobType") == "FilmStrip":
                fs = cd["data"].get("filmStrip", "")
                if fs.startswith("sh_knob_r") and fs.endswith(".png"):
                    try:
                        r_str = fs[len("sh_knob_r"):-len(".png")]
                        r = int(r_str)
                        size = 2 * r + 10
                        max_frames = min(FRAMES, MAX_IMAGE_H // size)
                        num_frames = cd["data"].get("numFrames")
                        frames = min(num_frames, max_frames) if num_frames else max_frames
                        cd["data"]["numFrames"] = frames
                        radii_used[r] = frames
                    except ValueError:
                        pass

    for r, frames in sorted(radii_used.items()):
        size = 2 * r + 10
        p = os.path.join(SKIN_DIR, "sh_knob_r%d.png" % r)
        print("  Generating Helm knob r=%d (%dx%d, %d frames)" % (r, size, size, frames))
        generate_knob_strip(size, r, p, frames)

    # 1b. Slider filmstrips (vertical and horizontal)
    sliders_used = {}
    for k, v in defs.items():
        if not k.startswith("shSlider"):
            continue
        for sub in v.get("componentsData", []):
            cd = sub["componentData"]
            if cd.get("type") == "Knob" and cd.get("data", {}).get("knobType") == "FilmStrip":
                fs = cd["data"].get("filmStrip", "")
                m = re.match(r"sh_slider_(v|h)_(\d+)x(\d+)(?:_.*)?\.png", fs)
                if m:
                    orient, w_s, h_s = m.group(1), int(m.group(2)), int(m.group(3))
                    frame_h = h_s if orient == "v" else max(w_s, h_s)
                    max_frames = min(FRAMES, MAX_IMAGE_H // frame_h)
                    num_frames = cd["data"].get("numFrames")
                    frames = min(num_frames, max_frames) if num_frames else max_frames
                    cd["data"]["numFrames"] = frames
                    is_step = ("step" in fs or "step" in k or (orient == "v" and w_s >= 30 and h_s <= 250))
                    sliders_used[(orient, w_s, h_s, fs)] = (frames, is_step)

    for (orient, sw, sh, fs), (frames, is_step) in sorted(sliders_used.items()):
        p = os.path.join(SKIN_DIR, fs)
        kind_str = "step bar" if is_step else ("vertical slider" if orient == "v" else "horizontal slider")
        print("  Generating Helm %s %s (%dx%d, %d frames)" % (kind_str, fs, sw, sh, frames))
        generate_slider_strip(sw, sh, orient, p, frames, is_step=is_step)

    # 2. Power buttons
    print("  Generating Helm circular illuminated power buttons (40x40)...")
    draw_power_button(40, False).save(os.path.join(SKIN_DIR, "sh_power_off.png"), optimize=True)
    draw_power_button(40, True).save(os.path.join(SKIN_DIR, "sh_power_on.png"), optimize=True)

    # 3. Flat custom buttons, selectors, popups, toggles, tiles, glowing switches
    options_map, switch_labels = build_options_map()
    repainted_count = 0

    if os.path.exists(SKIN_DIR):
        for f in sorted(os.listdir(SKIN_DIR)):
            if not f.endswith(".png"):
                continue
            f_path = os.path.join(SKIN_DIR, f)

            # Option segments: sh_seg_<key>_<index>_<on|off>.png
            if f.startswith("sh_seg_"):
                try:
                    name = f[len("sh_seg_"):-4]
                    key, idx_str, state = name.rsplit("_", 2)
                    idx = int(idx_str)
                    on = (state == "on")
                    opts = options_map.get(key, [])
                    label = opts[idx] if idx < len(opts) else ""
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_segment(w, h, label, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # Popup options: sh_popopt_<tab_i>_<key>_<index>_<on|off>.png
            elif f.startswith("sh_popopt_"):
                try:
                    name = f[len("sh_popopt_"):-4]
                    tab_i, rest = name.split("_", 1)
                    key, idx_str, state = rest.rsplit("_", 2)
                    idx = int(idx_str)
                    on = (state == "on")
                    opts = options_map.get(key, [])
                    label = opts[idx] if idx < len(opts) else ""
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_segment(w, h, label, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # In-section illuminated glowing switches: sh_switch_<key>_<on|off>.png
            elif f.startswith("sh_switch_"):
                try:
                    name = f[len("sh_switch_"):-4]
                    key, state = name.rsplit("_", 1)
                    on = (state == "on")
                    label = switch_labels.get(key, key.replace("_", " ").upper())
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_glowing_switch(w, h, label, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # In-section large toggle: sh_tog_<key>_<on|off>.png
            elif f.startswith("sh_tog_"):
                try:
                    name = f[len("sh_tog_"):-4]
                    state = name.rsplit("_", 1)[-1]
                    on = (state == "on")
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_large_toggle(w, h, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # Header toggle switches: sh_pill_<on|off>.png
            elif f.startswith("sh_pill_"):
                try:
                    state = f[len("sh_pill_"):-4]
                    on = (state == "on")
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_toggle_pill(w, h, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # In-parameter power buttons: sh_power_<on|off>.png
            elif f.startswith("sh_power_"):
                try:
                    state = f[len("sh_power_"):-4]
                    on = (state == "on")
                    draw_power_button(40, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # Action buttons: sh_btn_<key>_<label>_<on|off>.png
            elif f.startswith("sh_btn_"):
                try:
                    name = f[len("sh_btn_"):-4]
                    parts = name.rsplit("_", 1)
                    state = parts[-1]
                    on = (state == "on")
                    label = parts[0].rsplit("_", 1)[-1].upper()
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_segment(w, h, label, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # Preset list tiles: sh_tile_<WxH>_<on|off>.png
            elif f.startswith("sh_tile_"):
                try:
                    parts = f[len("sh_tile_"):-4].split("_")
                    state = parts[-1]
                    on = (state == "on")
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_tile(w, h, on).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

            # Stepper arrows: sh_arrow_<tab_i>_<key>_<side>.png
            elif f.startswith("sh_arrow_"):
                try:
                    side = f[:-4].rsplit("_", 1)[-1]
                    with Image.open(f_path) as orig:
                        w, h = orig.size
                    draw_arrow(w, h, side).save(f_path, optimize=True)
                    repainted_count += 1
                except Exception as e:
                    print("  Failed repainting %s: %s" % (f, e))

    print("  Repainted %d flat themed buttons/selectors/toggles/switches." % repainted_count)

    # 3. Restyle TUI component definitions
    for k, v in defs.items():
        # Knobs
        if k.startswith("shKnob"):
            for sub in v.get("componentsData", []):
                cd = sub.get("componentData", {})
                if cd.get("type") == "Focus":
                    cd.setdefault("data", {})["outlineColour"] = argb(CYAN_ACCENT)
                elif cd.get("name") == "Value":
                    ts = cd.get("data", {}).get("textStyle")
                    if ts:
                        ts["colour"] = argb(WHITE)
                elif cd.get("name") == "Name":
                    ts = cd.get("data", {}).get("textStyle")
                    if ts:
                        ts["colour"] = argb(DIM_WHITE)

        # Sliders
        elif k.startswith("shSlider"):
            for sub in v.get("componentsData", []):
                cd = sub.get("componentData", {})
                if cd.get("type") == "Focus":
                    cd.setdefault("data", {})["outlineColour"] = argb(CYAN_ACCENT)
                elif cd.get("name") == "Value":
                    ts = cd.get("data", {}).get("textStyle")
                    if ts:
                        ts["colour"] = argb(WHITE)
                elif cd.get("name") == "Name":
                    ts = cd.get("data", {}).get("textStyle")
                    if ts:
                        ts["colour"] = argb(DIM_WHITE)

        # Meters
        elif k.startswith("shMeter"):
            for sub in v.get("componentsData", []):
                cd = sub.get("componentData", {})
                if cd.get("type") == "Focus":
                    cd.setdefault("data", {})["outlineColour"] = argb(CYAN_ACCENT)

        # Selectors, popups, toggles, power buttons, glowing switches, triggers, rows
        elif (k.startswith("shSeg_") or k.startswith("shPopOpt_") or k.startswith("shToggle") or
              k.startswith("shPower") or k.startswith("shSwitch_") or k.startswith("shTrig_") or k.startswith("shRow_")):
            for sub in v.get("componentsData", []):
                cd = sub.get("componentData", {})
                if cd.get("type") == "Focus":
                    cd.setdefault("data", {})["outlineColour"] = argb(CYAN_ACCENT)
                elif cd.get("name") == "Name":
                    ts = cd.get("data", {}).get("textStyle")
                    if ts:
                        ts["colour"] = argb(DIM_WHITE)

    json.dump(tui, open(TUI_PATH, "w"), indent=1)
    print("Helm paint pass complete!")


if __name__ == "__main__":
    paint_helm()
