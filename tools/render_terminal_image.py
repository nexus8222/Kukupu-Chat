import re
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ANSI_STANDARD_COLORS = {
    0: (0, 0, 0),        # Black
    1: (205, 49, 49),    # Red
    2: (13, 188, 121),   # Green
    3: (229, 229, 16),   # Yellow
    4: (36, 114, 200),   # Blue
    5: (188, 63, 188),   # Magenta
    6: (17, 168, 205),   # Cyan
    7: (229, 229, 229),  # White
    8: (102, 102, 102),  # Bright Black / Gray
    9: (241, 76, 76),    # Bright Red
    10: (35, 209, 139),  # Bright Green
    11: (245, 245, 67),  # Bright Yellow
    12: (59, 142, 234),  # Bright Blue
    13: (214, 112, 214), # Bright Magenta
    14: (41, 184, 219),  # Bright Cyan
    15: (255, 255, 255)  # Bright White
}

def get_256_color(idx):
    if idx < 16:
        return ANSI_STANDARD_COLORS.get(idx, (200, 200, 200))
    if idx >= 232:
        val = 8 + (idx - 232) * 10
        return (val, val, val)
    idx -= 16
    b = (idx % 6) * 51 if (idx % 6) > 0 else 0
    idx //= 6
    g = (idx % 6) * 51 if (idx % 6) > 0 else 0
    idx //= 6
    r = (idx % 6) * 51 if (idx % 6) > 0 else 0
    return (r, g, b)

def parse_ansi_line(line_bytes):
    # Decode with replace
    text = line_bytes.decode('utf-8', errors='replace')
    # Filter out alternate screen buffer or cursor positioning escape codes
    text = re.sub(r'\x1b\[\?[0-9;]*[a-zA-Z]', '', text)
    text = re.sub(r'\x1b\[[0-9;]*[HJK]', '', text)
    text = text.replace('\a', '')

    segments = []
    current_color = (200, 200, 200)
    current_bold = False

    # Regex to match ANSI SGR: \x1b[...m
    pattern = re.compile(r'\x1b\[([0-9;]*)m')
    pos = 0

    for match in pattern.finditer(text):
        chunk = text[pos:match.start()]
        if chunk:
            segments.append((chunk, current_color, current_bold))
        pos = match.end()
        raw_codes = match.group(1).split(';') if match.group(1) else ['0']
        codes = [int(c) for c in raw_codes if c.isdigit()]

        i = 0
        while i < len(codes):
            c = codes[i]
            if c == 0:
                current_color = (200, 200, 200)
                current_bold = False
            elif c == 1:
                current_bold = True
            elif c == 2:
                # Dim
                current_color = (130, 130, 130)
            elif 30 <= c <= 37:
                base = c - 30
                current_color = ANSI_STANDARD_COLORS.get(base + 8 if current_bold else base, (200, 200, 200))
            elif c == 38 and i + 2 < len(codes) and codes[i+1] == 5:
                current_color = get_256_color(codes[i+2])
                i += 2
            elif 90 <= c <= 97:
                current_color = ANSI_STANDARD_COLORS.get(c - 90 + 8, (200, 200, 200))
            i += 1

    remaining = text[pos:]
    if remaining:
        segments.append((remaining, current_color, current_bold))

    return segments

def render_terminal_screenshot(raw_filename, out_filename, title="Kukupu Chat - Interactive Terminal"):
    with open(raw_filename, 'rb') as f:
        data = f.read()

    lines = data.split(b'\n')
    # Clean leading/trailing blank lines
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()

    # Font setup
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
    font_bold_path = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
    font_size = 15

    try:
        font_regular = ImageFont.truetype(font_path, font_size)
        font_bold = ImageFont.truetype(font_bold_path, font_size)
    except Exception:
        font_regular = ImageFont.load_default()
        font_bold = font_regular

    char_w = 9
    char_h = 19
    max_cols = 80
    max_rows = len(lines)

    pad_x = 18
    pad_y = 16
    titlebar_h = 36

    img_w = pad_x * 2 + max_cols * char_w
    img_h = titlebar_h + pad_y * 2 + max_rows * char_h

    # Create dark image
    bg_color = (15, 17, 26) # Sleek Cyber Dark
    img = Image.new('RGB', (img_w, img_h), bg_color)
    draw = ImageDraw.Draw(img)

    # Title bar background
    titlebar_bg = (24, 27, 38)
    draw.rectangle([0, 0, img_w, titlebar_h], fill=titlebar_bg)
    draw.line([0, titlebar_h, img_w, titlebar_h], fill=(40, 44, 60), width=1)

    # Window control dots
    draw.ellipse([14, 12, 24, 22], fill=(255, 95, 87))   # Close (Red)
    draw.ellipse([32, 12, 42, 22], fill=(254, 188, 46))  # Minimize (Yellow)
    draw.ellipse([50, 12, 60, 22], fill=(40, 201, 64))   # Maximize (Green)

    # Window Title
    title_box = draw.textbbox((0, 0), title, font=font_bold)
    title_w = title_box[2] - title_box[0]
    draw.text(((img_w - title_w) // 2, 9), title, fill=(160, 170, 195), font=font_bold)

    # Outer border
    draw.rectangle([0, 0, img_w - 1, img_h - 1], outline=(50, 56, 75), width=1)

    # Render lines
    y_start = titlebar_h + pad_y

    for row_idx, line in enumerate(lines):
        segments = parse_ansi_line(line)
        x_cursor = pad_x
        y_cursor = y_start + row_idx * char_h

        for text, color, is_bold in segments:
            f = font_bold if is_bold else font_regular
            for ch in text:
                draw.text((x_cursor, y_cursor), ch, fill=color, font=f)
                x_cursor += char_w

    img.save(out_filename, 'PNG')
    print(f"Generated screenshot: {out_filename} ({img_w}x{img_h})")

if __name__ == '__main__':
    render_terminal_screenshot("docs_screen_main.raw", "docs/images/tui_main_chat.png", "Kukupu Chat v1.2.0 — Interactive TUI")
    render_terminal_screenshot("docs_screen_account.raw", "docs/images/tui_account_portal.png", "Kukupu Chat — Account Management Portal")
    render_terminal_screenshot("docs_screen_party.raw", "docs/images/tui_party_portal.png", "Kukupu Chat — Party Portal & Moderation")
