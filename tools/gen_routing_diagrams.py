#!/usr/bin/env python3
"""Generate clean, professional, non-overlapping signal path diagrams for HelmX.
Exact mathematical grid layout with consistent pin spacings, uniform lane pitch,
and enlarged filter boxes (140x95) ensuring all oscillator lines connect well inside boxes.

Grid constants:
  Generators (x=40, w=115, h=46):
    OSC 1:   y=65..111   (center y=88)
    SUB OSC: y=145..191  (center y=168)
    NOISE:   y=225..271  (center y=248)
    OSC 2:   y=307..353  (center y=330)

  Filters (x=270, w=140, h=95):
    Filter 1: y=65..160   (Pins at y=88, 104, 120, 136 - pitch=16px)
    Filter 2: y=275..370  (Pins at y=298, 314, 330, 346 - pitch=16px)

  Vertical Trunk Lanes between x=155 and x=270:
    Lane A: x=185
    Lane B: x=205
    Lane C: x=225
    Lane D: x=245
    (Uniform pitch=20px)

  FX Column (x=740, w=140, h=58, gap=32px, pitch=90px):
    DISTORTION: y=75..133  (center y=104)
    DELAY:      y=165..223 (center y=194)
    REVERB:     y=255..313 (center y=284)
    STEREO OUT: y=345..403 (center y=374)
"""
import os
import math
import shutil
from PIL import Image, ImageDraw, ImageFont

W = 930
H = 530
SS = 2
SW = W * SS
SH = H * SS

# Colors
BG_COLOR = (18, 20, 24, 255)
BOX_BG = (26, 30, 36, 255)
BOX_BORDER = (55, 65, 80, 255)
LINE_CYAN = (32, 200, 255, 255)
LINE_AMBER = (255, 150, 0, 255)
LINE_SUB = (60, 220, 160, 255)
LINE_NOISE = (245, 205, 75, 255)
LINE_DIM = (70, 80, 95, 255)
TEXT_WHITE = (240, 244, 250, 255)
TEXT_DIM = (140, 150, 165, 255)
TEXT_CYAN = (32, 200, 255, 255)
TEXT_AMBER = (255, 150, 0, 255)
TEXT_SUB = (60, 220, 160, 255)
TEXT_NOISE = (245, 205, 75, 255)

FONT_REG = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/tools/html_art/fonts/TitilliumWeb-Regular.ttf"
FONT_BOLD = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/tools/html_art/fonts/TitilliumWeb-Bold.ttf"

def get_font(size, bold=False):
    fpath = FONT_BOLD if bold else FONT_REG
    if os.path.exists(fpath):
        return ImageFont.truetype(fpath, size * SS)
    return ImageFont.load_default()

def draw_arrow(draw, x1, y1, x2, y2, color, width=3*SS, arrow_size=9*SS):
    draw.line([(x1, y1), (x2, y2)], fill=color, width=width)
    dx = x2 - x1
    dy = y2 - y1
    dist = math.hypot(dx, dy)
    if dist > 0:
        ux = dx / dist
        uy = dy / dist
        px = -uy
        py = ux
        tip_x = x2
        tip_y = y2
        b1_x = x2 - ux * arrow_size + px * (arrow_size * 0.5)
        b1_y = y2 - uy * arrow_size + py * (arrow_size * 0.5)
        b2_x = x2 - ux * arrow_size - px * (arrow_size * 0.5)
        b2_y = y2 - uy * arrow_size - py * (arrow_size * 0.5)
        draw.polygon([(tip_x, tip_y), (b1_x, b1_y), (b2_x, b2_y)], fill=color)

def draw_ortho(draw, points, color, width=3*SS, arrow=True, arrow_size=9*SS):
    for i in range(len(points) - 1):
        x1, y1 = points[i][0] * SS, points[i][1] * SS
        x2, y2 = points[i+1][0] * SS, points[i+1][1] * SS
        if i == len(points) - 2 and arrow:
            draw_arrow(draw, x1, y1, x2, y2, color, width=width, arrow_size=arrow_size)
        else:
            draw.line([(x1, y1), (x2, y2)], fill=color, width=width)

def draw_junction(draw, x, y, color, r=4*SS):
    draw.ellipse([x*SS - r, y*SS - r, x*SS + r, y*SS + r], fill=color)

def draw_box(draw, x, y, w, h, title, subtitle="", border_color=BOX_BORDER, bg_color=BOX_BG, title_color=TEXT_WHITE, subtitle_color=TEXT_DIM):
    x, y, w, h = x * SS, y * SS, w * SS, h * SS
    r = 7 * SS
    draw.rounded_rectangle([x, y, x + w, y + h], radius=r, fill=bg_color, outline=border_color, width=2*SS)
    font_t = get_font(13, bold=True)
    draw.text((x + w/2, y + (h/2 if not subtitle else h/2 - 7*SS)), title, font=font_t, fill=title_color, anchor="mm")
    if subtitle:
        font_s = get_font(10, bold=False)
        draw.text((x + w/2, y + h/2 + 9*SS), subtitle, font=font_s, fill=subtitle_color, anchor="mm")

def draw_fx_stack(draw):
    fx_x = 740
    fx_w = 140
    fx_h = 58
    
    # DISTORTION
    draw_box(draw, fx_x, 75, fx_w, fx_h, "DISTORTION", "Drive / Mix", border_color=(75, 90, 110), title_color=TEXT_WHITE)
    # DELAY
    draw_box(draw, fx_x, 165, fx_w, fx_h, "DELAY", "Stereo Ping-Pong", border_color=(75, 90, 110), title_color=TEXT_WHITE)
    # REVERB
    draw_box(draw, fx_x, 255, fx_w, fx_h, "REVERB", "Damping / Wet", border_color=(75, 90, 110), title_color=TEXT_WHITE)
    # STEREO OUT (Output Node)
    draw_box(draw, fx_x, 345, fx_w, fx_h, "STEREO OUT", "Master L / R", border_color=LINE_CYAN, title_color=TEXT_CYAN)

    # Vertical connections with arrows
    mid_x = fx_x + fx_w // 2  # 810
    draw_arrow(draw, mid_x * SS, (75 + fx_h) * SS, mid_x * SS, 165 * SS, LINE_CYAN)
    draw_arrow(draw, mid_x * SS, (165 + fx_h) * SS, mid_x * SS, 255 * SS, LINE_CYAN)
    draw_arrow(draw, mid_x * SS, (255 + fx_h) * SS, mid_x * SS, 345 * SS, LINE_CYAN)

MODE_NAMES = [
    "MODE 0: SERIES (FILTER 1  >  FILTER 2)",
    "MODE 1: PARALLEL (OSC 1  >  F1,  OSC 2  >  F2)",
    "MODE 2: SPLIT 1 (OSC 1  >  F1  >  F2,  OSC 2  >  F2)",
    "MODE 3: SPLIT 2 (OSC 2  >  F1  >  F2,  OSC 1  >  F2)",
]

TARGET_LABELS = ["BOTH", "FILTER 1", "FILTER 2"]

def generate_diagram(f_mode, sub_target, noise_target, out_path):
    im = Image.new("RGBA", (SW, SH), BG_COLOR)
    draw = ImageDraw.Draw(im)

    # Header
    f_hdr = get_font(15, bold=True)
    draw.text((40*SS, 22*SS), MODE_NAMES[f_mode], font=f_hdr, fill=TEXT_AMBER)
    f_sub = get_font(11, bold=False)
    sub_text = f"Sub Route: {TARGET_LABELS[sub_target]}     |     Noise Route: {TARGET_LABELS[noise_target]}"
    draw.text((40*SS, 46*SS), sub_text, font=f_sub, fill=TEXT_DIM)

    # Right side FX Stack (Fixed position)
    draw_fx_stack(draw)

    # Sources (Left column): OSC 1 (top), SUB, NOISE, OSC 2 (bottom)
    gen_x = 40
    gen_w = 115
    gen_h = 46
    gx_out = gen_x + gen_w  # 155

    sub_subt = f"Route: {TARGET_LABELS[sub_target]}"
    noise_subt = f"Route: {TARGET_LABELS[noise_target]}"

    draw_box(draw, gen_x, 65, gen_w, gen_h, "OSC 1", "Wave / Pan / Spread")
    draw_box(draw, gen_x, 145, gen_w, gen_h, "SUB OSC", sub_subt, border_color=LINE_SUB, title_color=TEXT_SUB)
    draw_box(draw, gen_x, 225, gen_w, gen_h, "NOISE", noise_subt, border_color=LINE_NOISE, title_color=TEXT_NOISE)
    draw_box(draw, gen_x, 307, gen_w, gen_h, "OSC 2", "Wave / Pan / Spread")

    osc1_cy = 65 + gen_h // 2   # 88
    sub_cy  = 145 + gen_h // 2  # 168
    noise_cy = 225 + gen_h // 2 # 248
    osc2_cy = 307 + gen_h // 2  # 330

    # Fixed enlarged filter positions across all modes
    f1_x, f1_y, f1_w, f1_h = 270, 65, 140, 95
    f2_x, f2_y, f2_w, f2_h = 270, 275, 140, 95

    draw_box(draw, f1_x, f1_y, f1_w, f1_h, "FILTER 1", "Cutoff / Res / Pan", border_color=LINE_CYAN, title_color=TEXT_CYAN)
    draw_box(draw, f2_x, f2_y, f2_w, f2_h, "FILTER 2", "Cutoff / Res / Pan", border_color=LINE_AMBER, title_color=TEXT_AMBER)

    mid_filter_x = f1_x + f1_w // 2  # 340

    # Filter 1 input pin Y positions (pitch = 16px):
    f1_pin1 = 88   # OSC 1
    f1_pin2 = 104  # SUB OSC
    f1_pin3 = 120  # NOISE
    f1_pin4 = 136  # OSC 2

    # Filter 2 input pin Y positions (pitch = 16px):
    f2_pin1 = 298  # SUB OSC
    f2_pin2 = 314  # NOISE
    f2_pin3 = 330  # OSC 2
    f2_pin4 = 346  # OSC 1 (Split 2)

    # Vertical trunk lanes (pitch = 20px):
    lane_a = 185
    lane_b = 205
    lane_c = 225
    lane_d = 245

    # =========================================================================
    # MODE 0: SERIES (Filter 1 -> connection DOWN -> Filter 2 -> FX)
    # =========================================================================
    if f_mode == 0:
        # Straight vertical connection DOWN between Filter 1 and Filter 2
        draw_arrow(draw, mid_filter_x * SS, (f1_y + f1_h) * SS, mid_filter_x * SS, f2_y * SS, LINE_CYAN)

        # OSC 1 -> F1 (straight horizontal)
        draw_ortho(draw, [(gx_out, osc1_cy), (f1_x, f1_pin1)], LINE_CYAN)

        # OSC 2 -> F1 (turns up at lane_b=205 into f1_pin4=136)
        draw_ortho(draw, [(gx_out, osc2_cy), (lane_b, osc2_cy), (lane_b, f1_pin4), (f1_x, f1_pin4)], LINE_AMBER)

        # Sub Osc:
        if sub_target == 0 or sub_target == 1:
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f1_pin2), (f1_x, f1_pin2)], LINE_SUB)
        elif sub_target == 2:
            # Bypasses F1, turns down directly into F2 pin1
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f2_pin1), (f2_x, f2_pin1)], LINE_SUB)

        # Noise:
        if noise_target == 0 or noise_target == 1:
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f1_pin3), (f1_x, f1_pin3)], LINE_NOISE)
        elif noise_target == 2:
            # Bypasses F1, turns down directly into F2 pin2
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f2_pin2), (f2_x, f2_pin2)], LINE_NOISE)

        # F2 output -> DISTORTION
        draw_ortho(draw, [(f2_x + f2_w, 322), (665, 322), (665, 104), (740, 104)], LINE_CYAN)

    # =========================================================================
    # MODE 1: PARALLEL (OSC 1 -> F1, OSC 2 -> F2, F1+F2 -> SUM -> FX)
    # =========================================================================
    elif f_mode == 1:
        sum_x, sum_y, sum_w, sum_h = 510, 187, 84, 60
        draw_box(draw, sum_x, sum_y, sum_w, sum_h, "SUM", "Mix L+R", border_color=LINE_CYAN)

        # OSC 1 -> F1 (straight horizontal at y=88)
        draw_ortho(draw, [(gx_out, osc1_cy), (f1_x, f1_pin1)], LINE_CYAN)

        # OSC 2 -> F2 (straight horizontal at y=330, right into F2 pin3)
        draw_ortho(draw, [(gx_out, osc2_cy), (f2_x, f2_pin3)], LINE_AMBER)

        # Sub Osc:
        if sub_target == 0:  # Both
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy)], LINE_SUB, arrow=False)
            draw_junction(draw, lane_d, sub_cy, LINE_SUB)
            draw_ortho(draw, [(lane_d, sub_cy), (lane_d, f1_pin2), (f1_x, f1_pin2)], LINE_SUB)
            draw_ortho(draw, [(lane_d, sub_cy), (lane_d, f2_pin1), (f2_x, f2_pin1)], LINE_SUB)
        elif sub_target == 1:  # F1
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f1_pin2), (f1_x, f1_pin2)], LINE_SUB)
        elif sub_target == 2:  # F2
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f2_pin1), (f2_x, f2_pin1)], LINE_SUB)

        # Noise:
        if noise_target == 0:  # Both
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy)], LINE_NOISE, arrow=False)
            draw_junction(draw, lane_c, noise_cy, LINE_NOISE)
            draw_ortho(draw, [(lane_c, noise_cy), (lane_c, f1_pin3), (f1_x, f1_pin3)], LINE_NOISE)
            draw_ortho(draw, [(lane_c, noise_cy), (lane_c, f2_pin2), (f2_x, f2_pin2)], LINE_NOISE)
        elif noise_target == 1:  # F1
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f1_pin3), (f1_x, f1_pin3)], LINE_NOISE)
        elif noise_target == 2:  # F2
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f2_pin2), (f2_x, f2_pin2)], LINE_NOISE)

        # F1 -> SUM (symmetric pitch)
        draw_ortho(draw, [(f1_x + f1_w, 112), (460, 112), (460, 204), (sum_x, 204)], LINE_CYAN)

        # F2 -> SUM (symmetric pitch)
        draw_ortho(draw, [(f2_x + f2_w, 322), (460, 322), (460, 230), (sum_x, 230)], LINE_AMBER)

        # SUM -> DISTORTION
        draw_ortho(draw, [(sum_x + sum_w, 217), (665, 217), (665, 104), (740, 104)], LINE_CYAN)

    # =========================================================================
    # MODE 2: SPLIT 1 (OSC 1 -> F1 -> connection DOWN -> F2, OSC 2 -> F2 only)
    # =========================================================================
    elif f_mode == 2:
        # Connection DOWN between Filter 1 and Filter 2
        draw_arrow(draw, mid_filter_x * SS, (f1_y + f1_h) * SS, mid_filter_x * SS, f2_y * SS, LINE_CYAN)

        # OSC 1 -> F1 (straight horizontal)
        draw_ortho(draw, [(gx_out, osc1_cy), (f1_x, f1_pin1)], LINE_CYAN)

        # OSC 2 -> F2 (straight horizontal directly into F2 pin3)
        draw_ortho(draw, [(gx_out, osc2_cy), (f2_x, f2_pin3)], LINE_AMBER)

        # Sub Osc:
        if sub_target == 0 or sub_target == 1:
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f1_pin2), (f1_x, f1_pin2)], LINE_SUB)
        elif sub_target == 2:
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f2_pin1), (f2_x, f2_pin1)], LINE_SUB)

        # Noise:
        if noise_target == 0 or noise_target == 1:
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f1_pin3), (f1_x, f1_pin3)], LINE_NOISE)
        elif noise_target == 2:
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f2_pin2), (f2_x, f2_pin2)], LINE_NOISE)

        # F2 output -> DISTORTION
        draw_ortho(draw, [(f2_x + f2_w, 322), (665, 322), (665, 104), (740, 104)], LINE_CYAN)

    # =========================================================================
    # MODE 3: SPLIT 2 (OSC 2 -> F1 -> connection DOWN -> F2, OSC 1 -> F2 only)
    # =========================================================================
    elif f_mode == 3:
        # Connection DOWN between Filter 1 and Filter 2
        draw_arrow(draw, mid_filter_x * SS, (f1_y + f1_h) * SS, mid_filter_x * SS, f2_y * SS, LINE_CYAN)

        # OSC 2 -> F1 (turns up at lane_b=205 into f1_pin4=136)
        draw_ortho(draw, [(gx_out, osc2_cy), (lane_b, osc2_cy), (lane_b, f1_pin4), (f1_x, f1_pin4)], LINE_AMBER)

        # OSC 1 -> F2 (direct into F2 pin4=346 via lane_a=185)
        draw_ortho(draw, [(gx_out, osc1_cy), (lane_a, osc1_cy), (lane_a, f2_pin4), (f2_x, f2_pin4)], LINE_CYAN)

        # Sub Osc:
        if sub_target == 0 or sub_target == 1:
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f1_pin2), (f1_x, f1_pin2)], LINE_SUB)
        elif sub_target == 2:
            draw_ortho(draw, [(gx_out, sub_cy), (lane_d, sub_cy), (lane_d, f2_pin1), (f2_x, f2_pin1)], LINE_SUB)

        # Noise:
        if noise_target == 0 or noise_target == 1:
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f1_pin3), (f1_x, f1_pin3)], LINE_NOISE)
        elif noise_target == 2:
            draw_ortho(draw, [(gx_out, noise_cy), (lane_c, noise_cy), (lane_c, f2_pin2), (f2_x, f2_pin2)], LINE_NOISE)

        # F2 output -> DISTORTION
        draw_ortho(draw, [(f2_x + f2_w, 322), (665, 322), (665, 104), (740, 104)], LINE_CYAN)

    im_resized = im.resize((W, H), Image.LANCZOS)
    im_resized.save(out_path)

def main():
    img_dir = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/images"
    os.makedirs(img_dir, exist_ok=True)

    print("Generating 36 routing diagrams...")
    for f in range(4):
        for s in range(3):
            for n in range(3):
                idx = f * 9 + s * 3 + n
                out_path = os.path.join(img_dir, f"routing_{idx}.png")
                generate_diagram(f, s, n, out_path)

    # Save aliases
    shutil.copyfile(os.path.join(img_dir, "routing_0.png"), os.path.join(img_dir, "routing_series.png"))
    shutil.copyfile(os.path.join(img_dir, "routing_9.png"), os.path.join(img_dir, "routing_parallel.png"))
    shutil.copyfile(os.path.join(img_dir, "routing_18.png"), os.path.join(img_dir, "routing_split1.png"))
    shutil.copyfile(os.path.join(img_dir, "routing_27.png"), os.path.join(img_dir, "routing_split2.png"))
    print("Done! All 36 diagrams regenerated successfully.")

if __name__ == "__main__":
    main()
