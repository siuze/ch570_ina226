import math
from PIL import Image, ImageDraw, ImageFont

def draw_puzzle_icon():
    scale = 2
    size = 512 * scale
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    blue_line = (2, 132, 199, 255)       # #0284C7
    blue_fill = (240, 249, 255, 255)      # #F0F9FF
    line_w = int(14 * scale)

    pad = int(42 * scale)
    span = size - pad * 2
    half = span // 2
    tab_r = int(32 * scale)
    gap = int(6 * scale)

    # 4 piece rectangles:
    # TL: (x0, y0) to (x0 + half - gap, y0 + half - gap)
    # TR: (x0 + half + gap, y0) to (x1, y0 + half - gap)
    # BL: (x0, y0 + half + gap) to (x0 + half - gap, y1)
    # BR: (x0 + half + gap, y0 + half + gap) to (x1, y1)

    tl_rect = [pad, pad, pad + half - gap, pad + half - gap]
    tr_rect = [pad + half + gap, pad, pad + span, pad + half - gap]
    bl_rect = [pad, pad + half + gap, pad + half - gap, pad + span]
    br_rect = [pad + half + gap, pad + half + gap, pad + span, pad + span]

    # Draw rounded rectangle base for the 4 pieces
    r_corner = int(12 * scale)
    for rect in [tl_rect, tr_rect, bl_rect, br_rect]:
        draw.rounded_rectangle(rect, radius=r_corner, fill=blue_fill, outline=blue_line, width=line_w)

    # Now draw the jigsaw interlocking tabs / connectors:
    # 1. Horizontal connector between TL and TR (outward from TL into gap)
    conn_y = pad + half // 2 - gap // 2
    tl_right_x = tl_rect[2]
    # Draw circular tab on TL's right edge
    draw.ellipse([tl_right_x - tab_r + 4, conn_y - tab_r, tl_right_x + tab_r + 4, conn_y + tab_r], fill=blue_fill, outline=blue_line, width=line_w)
    # Overwrite the interior chord with fill to make it seamless
    draw.rectangle([tl_right_x - line_w - 4, conn_y - tab_r + line_w, tl_right_x, conn_y + tab_r - line_w], fill=blue_fill)

    # 2. Vertical connector between TL and BL (outward from TL down)
    conn_x = pad + half // 2 - gap // 2
    tl_bottom_y = tl_rect[3]
    draw.ellipse([conn_x - tab_r, tl_bottom_y - tab_r + 4, conn_x + tab_r, tl_bottom_y + tab_r + 4], fill=blue_fill, outline=blue_line, width=line_w)
    draw.rectangle([conn_x - tab_r + line_w, tl_bottom_y - line_w - 4, conn_x + tab_r - line_w, tl_bottom_y], fill=blue_fill)

    # 3. Vertical connector between TR and BR (outward from TR down)
    conn_tr_x = pad + half + gap + (half - gap) // 2
    tr_bottom_y = tr_rect[3]
    draw.ellipse([conn_tr_x - tab_r, tr_bottom_y - tab_r + 4, conn_tr_x + tab_r, tr_bottom_y + tab_r + 4], fill=blue_fill, outline=blue_line, width=line_w)
    draw.rectangle([conn_tr_x - tab_r + line_w, tr_bottom_y - line_w - 4, conn_tr_x + tab_r - line_w, tr_bottom_y], fill=blue_fill)

    # 4. Horizontal connector between BL and BR (outward from BL right)
    bl_conn_y = pad + half + gap + (half - gap) // 2
    bl_right_x = bl_rect[2]
    draw.ellipse([bl_right_x - tab_r + 4, bl_conn_y - tab_r, bl_right_x + tab_r + 4, bl_conn_y + tab_r], fill=blue_fill, outline=blue_line, width=line_w)
    draw.rectangle([bl_right_x - line_w - 4, bl_conn_y - tab_r + line_w, bl_right_x, bl_conn_y + tab_r - line_w], fill=blue_fill)

    # Corresponding inward sockets (drawn on TR and BR)
    # On TR left: an inward arc
    tr_left_x = tr_rect[0]
    draw.arc([tr_left_x - tab_r - 4, conn_y - tab_r, tr_left_x + tab_r - 4, conn_y + tab_r], start=270, end=90, fill=blue_line, width=line_w)

    # On BR left: an inward arc
    br_left_x = br_rect[0]
    draw.arc([br_left_x - tab_r - 4, bl_conn_y - tab_r, br_left_x + tab_r - 4, bl_conn_y + tab_r], start=270, end=90, fill=blue_line, width=line_w)

    # On BL top: an inward arc
    bl_top_y = bl_rect[1]
    draw.arc([conn_x - tab_r, bl_top_y - tab_r - 4, conn_x + tab_r, bl_top_y + tab_r - 4], start=0, end=180, fill=blue_line, width=line_w)

    # On BR top: an inward arc
    br_top_y = br_rect[1]
    draw.arc([conn_tr_x - tab_r, br_top_y - tab_r - 4, conn_tr_x + tab_r, br_top_y + tab_r - 4], start=0, end=180, fill=blue_line, width=line_w)

    # Decorative tech lines in TR and BL
    lw_inner = int(10 * scale)
    draw.line([(tr_rect[0] + 35 * scale, tr_rect[1] + 45 * scale), (tr_rect[2] - 35 * scale, tr_rect[1] + 45 * scale)], fill=blue_line, width=lw_inner)
    draw.line([(tr_rect[0] + 35 * scale, tr_rect[1] + 75 * scale), (tr_rect[2] - 65 * scale, tr_rect[1] + 75 * scale)], fill=blue_line, width=lw_inner)

    draw.line([(bl_rect[0] + 35 * scale, bl_rect[3] - 75 * scale), (bl_rect[2] - 35 * scale, bl_rect[3] - 75 * scale)], fill=blue_line, width=lw_inner)
    draw.line([(bl_rect[0] + 35 * scale, bl_rect[3] - 45 * scale), (bl_rect[2] - 65 * scale, bl_rect[3] - 45 * scale)], fill=blue_line, width=lw_inner)

    # -------------------------------------------------------------
    # INSIDE TOP-LEFT PIECE: Ammeter Symbol -( A )-
    # -------------------------------------------------------------
    amm_cx = (tl_rect[0] + tl_rect[2]) // 2
    amm_cy = (tl_rect[1] + tl_rect[3]) // 2
    amm_r = int(45 * scale)
    lw_amm = int(11 * scale)

    # Horizontal connection leads
    draw.line([(amm_cx - amm_r - 30 * scale, amm_cy), (amm_cx - amm_r, amm_cy)], fill=blue_line, width=lw_amm)
    draw.line([(amm_cx + amm_r, amm_cy), (amm_cx + amm_r + 30 * scale, amm_cy)], fill=blue_line, width=lw_amm)
    # Circle
    draw.ellipse([amm_cx - amm_r, amm_cy - amm_r, amm_cx + amm_r, amm_cy + amm_r], outline=blue_line, width=lw_amm)

    # Letter A
    try:
        font = ImageFont.truetype("C:\\Windows\\Fonts\\arialbd.ttf", int(54 * scale))
    except:
        font = ImageFont.load_default()

    bbox = font.getbbox("A")
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    draw.text((amm_cx - tw // 2 - bbox[0], amm_cy - th // 2 - bbox[1] - 2 * scale), "A", fill=blue_line, font=font)

    # -------------------------------------------------------------
    # INSIDE BOTTOM-RIGHT PIECE: Square Waveform _|-|_|--
    # -------------------------------------------------------------
    wx0 = br_rect[0] + 32 * scale
    wx1 = br_rect[2] - 32 * scale
    wy_mid = (br_rect[1] + br_rect[3]) // 2
    wy_h = wy_mid - int(34 * scale)
    wy_l = wy_mid + int(34 * scale)

    step_w = (wx1 - wx0) / 6.0
    wave_pts = [
        (wx0, wy_h),
        (wx0 + step_w * 0.8, wy_h),
        (wx0 + step_w * 0.8, wy_l),
        (wx0 + step_w * 2.0, wy_l),
        (wx0 + step_w * 2.0, wy_h),
        (wx0 + step_w * 3.2, wy_h),
        (wx0 + step_w * 3.2, wy_l),
        (wx0 + step_w * 4.4, wy_l),
        (wx0 + step_w * 4.4, wy_h),
        (wx1, wy_h)
    ]
    for i in range(len(wave_pts) - 1):
        draw.line([wave_pts[i], wave_pts[i+1]], fill=blue_line, width=lw_amm, joint='curve')

    # Downsample with LANCZOS to 512x512
    final_img = img.resize((512, 512), Image.Resampling.LANCZOS)
    return final_img

if __name__ == '__main__':
    img = draw_puzzle_icon()
    img.save('src/app_icon.png', 'PNG')
    print("Saved antialiased src/app_icon.png")

    icon_sizes = [(16,16), (24,24), (32,32), (48,48), (64,64), (128,128), (256,256)]
    img.save('src/app.ico', format='ICO', sizes=icon_sizes)
    print("Saved multi-res src/app.ico")
