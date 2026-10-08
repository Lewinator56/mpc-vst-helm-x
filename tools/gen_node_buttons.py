#!/usr/bin/env python3
"""Generate high-resolution, pixel-perfect ON and OFF state button images
for Filter 1, Filter 2, and the FX stack (Distortion, Delay, Reverb).

Exact dimensions matching the routing diagram node geometry:
  Filter 1:    140 x 95
  Filter 2:    140 x 95
  Distortion:  140 x 58
  Delay:       140 x 58
  Reverb:      140 x 58
"""
import os
from PIL import Image, ImageDraw, ImageFont

SS = 4  # 4x supersampling for ultra-crisp vector-like rendering downsampled with Lanczos
FONT_REG = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/tools/html_art/fonts/TitilliumWeb-Regular.ttf"
FONT_BOLD = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/tools/html_art/fonts/TitilliumWeb-Bold.ttf"
FONT_SEMI = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/tools/html_art/fonts/TitilliumWeb-SemiBold.ttf"

def get_font(size, bold=False, semibold=False):
    fpath = FONT_BOLD if bold else (FONT_SEMI if semibold else FONT_REG)
    if os.path.exists(fpath):
        return ImageFont.truetype(fpath, int(size * SS))
    return ImageFont.load_default()

def create_filter_button(title, subtitle, accent_color, is_on, out_path):
    W, H = 140, 95
    SW, SH = W * SS, H * SS
    
    im = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    
    radius = 7 * SS
    border_w = int(2.5 * SS)
    
    # Coordinates for box
    inset = border_w // 2 + 1
    x0, y0 = inset, inset
    x1, y1 = SW - inset - 1, SH - inset - 1
    
    if is_on:
        # ON State: Dark sleek slate with subtle tint, glowing vibrant border
        bg_col = (20, 24, 30, 255)
        border_col = accent_color
        title_col = (248, 252, 255, 255)
        sub_col = accent_color
        badge_bg = (accent_color[0] // 5, accent_color[1] // 5, accent_color[2] // 5, 255)
        badge_border = accent_color
        badge_text_col = accent_color
        led_col = accent_color
        badge_text = "ACTIVE"
    else:
        # OFF State: Recessed dimmed dark card, muted gray border, BYPASS badge
        bg_col = (15, 17, 21, 255)
        border_col = (52, 60, 72, 255)
        title_col = (110, 120, 135, 255)
        sub_col = (75, 84, 96, 255)
        badge_bg = (24, 26, 30, 255)
        badge_border = (65, 72, 84, 255)
        badge_text_col = (140, 120, 90, 255)
        led_col = (70, 75, 85, 255)
        badge_text = "BYPASS"

    # Draw card body
    draw.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=bg_col, outline=border_col, width=border_w)
    
    # Title
    f_title = get_font(13.5, bold=True)
    draw.text((SW // 2, int(25 * SS)), title, font=f_title, fill=title_col, anchor="mm")
    
    # Subtitle
    f_sub = get_font(10, semibold=True)
    draw.text((SW // 2, int(45 * SS)), subtitle, font=f_sub, fill=sub_col, anchor="mm")
    
    # Bottom Status Pill
    pw = int(72 * SS)
    ph = int(20 * SS)
    px0 = (SW - pw) // 2
    py0 = int(62 * SS)
    px1 = px0 + pw
    py1 = py0 + ph
    pradius = int(6 * SS)
    
    draw.rounded_rectangle([px0, py0, px1, py1], radius=pradius, fill=badge_bg, outline=badge_border, width=int(1.5 * SS))
    
    # LED dot inside pill
    led_r = int(3.5 * SS)
    led_cx = px0 + int(14 * SS)
    led_cy = (py0 + py1) // 2
    draw.ellipse([led_cx - led_r, led_cy - led_r, led_cx + led_r, led_cy + led_r], fill=led_col)
    
    # Badge text
    f_badge = get_font(9.5, bold=True)
    text_x = px0 + int(42 * SS)
    draw.text((text_x, led_cy), badge_text, font=f_badge, fill=badge_text_col, anchor="mm")
    
    # Downsample with Lanczos
    resample_filter = getattr(Image, 'Resampling', Image).LANCZOS
    final_im = im.resize((W, H), resample_filter)
    final_im.save(out_path, "PNG")

def create_fx_button(title, subtitle, accent_color, is_on, out_path, H=56):
    W = 140
    SW, SH = W * SS, H * SS
    
    im = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    
    radius = 7 * SS
    border_w = int(2.5 * SS)
    
    inset = border_w // 2 + 1
    x0, y0 = inset, inset
    x1, y1 = SW - inset - 1, SH - inset - 1
    
    if is_on:
        bg_col = (20, 24, 30, 255)
        border_col = accent_color
        title_col = (248, 252, 255, 255)
        sub_col = accent_color
        led_col = accent_color
        status_text = "ON"
        status_col = accent_color
    else:
        bg_col = (15, 17, 21, 255)
        border_col = (52, 60, 72, 255)
        title_col = (110, 120, 135, 255)
        sub_col = (75, 84, 96, 255)
        led_col = (65, 72, 82, 255)
        status_text = "OFF"
        status_col = (130, 115, 90, 255)

    draw.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=bg_col, outline=border_col, width=border_w)
    
    # Title
    f_title = get_font(12, bold=True)
    draw.text((int(62 * SS), int(18 * SS)), title, font=f_title, fill=title_col, anchor="mm")
    
    # Subtitle
    f_sub = get_font(9.5, semibold=False)
    sub_text = subtitle if is_on else "Bypassed"
    draw.text((int(62 * SS), int(36 * SS)), sub_text, font=f_sub, fill=sub_col, anchor="mm")
    
    # Right-hand side power badge
    bx0 = int(106 * SS)
    by0 = int(13 * SS)
    bx1 = int(131 * SS)
    by1 = int(43 * SS)
    draw.rounded_rectangle([bx0, by0, bx1, by1], radius=int(5 * SS),
                           fill=(bg_col[0] // 2, bg_col[1] // 2, bg_col[2] // 2, 255),
                           outline=border_col, width=int(1.5 * SS))
    
    # LED inside badge
    led_r = int(3 * SS)
    led_cx = (bx0 + bx1) // 2
    led_cy = by0 + int(9 * SS)
    draw.ellipse([led_cx - led_r, led_cy - led_r, led_cx + led_r, led_cy + led_r], fill=led_col)
    
    f_status = get_font(7.5, bold=True)
    draw.text((led_cx, by0 + int(21 * SS)), status_text, font=f_status, fill=status_col, anchor="mm")
    
    resample_filter = getattr(Image, 'Resampling', Image).LANCZOS
    final_im = im.resize((W, H), resample_filter)
    final_im.save(out_path, "PNG")

def main():
    img_dir = "/home/ubuntu/mpc-vst/new/mpc-vst-helm-x/images"
    os.makedirs(img_dir, exist_ok=True)
    
    # Colors
    CYAN = (32, 200, 255, 255)
    AMBER = (255, 150, 0, 255)
    EQ_BLUE = (0, 220, 255, 255)
    DIST_RED = (255, 85, 65, 255)
    DELAY_TEAL = (32, 215, 185, 255)
    REVERB_PURPLE = (175, 125, 255, 255)
    
    print("Generating Filter 1 button images...")
    create_filter_button("FILTER 1", "Cutoff / Res / Pan", CYAN, True, os.path.join(img_dir, "btn_f1_on.png"))
    create_filter_button("FILTER 1", "Cutoff / Res / Pan", CYAN, False, os.path.join(img_dir, "btn_f1_off.png"))
    
    print("Generating Filter 2 button images...")
    create_filter_button("FILTER 2", "Cutoff / Res / Pan", AMBER, True, os.path.join(img_dir, "btn_f2_on.png"))
    create_filter_button("FILTER 2", "Cutoff / Res / Pan", AMBER, False, os.path.join(img_dir, "btn_f2_off.png"))
    
    print("Generating FX button images...")
    create_fx_button("EQUALIZER", "5-Band EQ", EQ_BLUE, True, os.path.join(img_dir, "btn_eq_on.png"))
    create_fx_button("EQUALIZER", "5-Band EQ", EQ_BLUE, False, os.path.join(img_dir, "btn_eq_off.png"))

    create_fx_button("DISTORTION", "Drive / Mix", DIST_RED, True, os.path.join(img_dir, "btn_dist_on.png"))
    create_fx_button("DISTORTION", "Drive / Mix", DIST_RED, False, os.path.join(img_dir, "btn_dist_off.png"))
    
    create_fx_button("DELAY", "Stereo Ping-Pong", DELAY_TEAL, True, os.path.join(img_dir, "btn_delay_on.png"))
    create_fx_button("DELAY", "Stereo Ping-Pong", DELAY_TEAL, False, os.path.join(img_dir, "btn_delay_off.png"))
    
    create_fx_button("REVERB", "Damping / Wet", REVERB_PURPLE, True, os.path.join(img_dir, "btn_reverb_on.png"))
    create_fx_button("REVERB", "Damping / Wet", REVERB_PURPLE, False, os.path.join(img_dir, "btn_reverb_off.png"))
    
    print("Successfully generated all 12 node button images.")

if __name__ == "__main__":
    main()
