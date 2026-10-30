"""Draws GRAVE PRIX's icon: a tombstone with a chequered band across it, two black tyre tracks swerving past, on a
blood-red rounded square under a pale moon.

Writes res/graveprix.ico (16 to 256 px, the Windows program icon), res/graveprix.png (512 px) and src/IconPng.h
(a 128 px PNG as bytes: the window's icon on every platform).

    python tools/make_icon.py
"""
import io
import math
import os

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 2048   # drawn this large, then scaled down smoothly


def layer():
    return Image.new('RGBA', (S, S), (0, 0, 0, 0))


def icon():
    img = layer()
    base = layer()
    ImageDraw.Draw(base).rounded_rectangle([40, 40, S - 40, S - 40], radius=S // 6, fill=(96, 10, 14, 255))
    mask = base.split()[3]
    # A dark red sky and a pale moon.
    sky = layer()
    d = ImageDraw.Draw(sky)
    for i in range(64):
        y = S * i / 64
        c = int(30 + 70 * i / 64)
        d.rectangle([0, y, S, y + S / 64 + 2], fill=(c, 6, 10, 255))
    moon = layer()
    ImageDraw.Draw(moon).ellipse([S * 0.64, S * 0.1, S * 0.88, S * 0.34], fill=(240, 226, 200, 255))
    moon = Image.alpha_composite(moon.filter(ImageFilter.GaussianBlur(S * 0.03)), moon)
    sky = Image.alpha_composite(sky, moon)
    img = Image.alpha_composite(img, Image.composite(sky, layer(), mask))
    # Two tyre tracks swerving across.
    tracks = layer()
    td = ImageDraw.Draw(tracks)
    for off in (-0.06, 0.06):
        pts = []
        for i in range(81):
            t = i / 80
            x = S * (0.04 + 0.92 * t)
            y = S * (0.88 + off - 0.2 * math.sin(t * math.pi * 1.4) - 0.06 * t)
            pts.append((x, y))
        td.line(pts, fill=(14, 10, 10, 235), width=int(S * 0.045), joint='curve')
    img = Image.alpha_composite(img, Image.composite(tracks, layer(), mask))
    # The tombstone.
    stone = layer()
    sd = ImageDraw.Draw(stone)
    x0, x1, y0, y1 = S * 0.27, S * 0.73, S * 0.18, S * 0.86
    sd.rectangle([x0, y0 + (x1 - x0) / 2, x1, y1], fill=(176, 172, 162, 255))
    sd.ellipse([x0, y0, x1, y0 + (x1 - x0)], fill=(176, 172, 162, 255))
    # A chequered band across it.
    band = layer()
    bd = ImageDraw.Draw(band)
    n, by0, by1 = 8, S * 0.5, S * 0.62
    w = (x1 - x0) / n
    for i in range(n):
        for j in range(2):
            col = (20, 20, 22, 255) if (i + j) % 2 == 0 else (240, 236, 228, 255)
            bd.rectangle([x0 + i * w, by0 + j * (by1 - by0) / 2, x0 + (i + 1) * w, by0 + (j + 1) * (by1 - by0) / 2], fill=col)
    stone = Image.alpha_composite(stone, Image.composite(band, layer(), stone.split()[3]))
    # A cross chiselled in above the band.
    sd = ImageDraw.Draw(stone)
    cx, cy = S * 0.5, S * 0.36
    groove = (88, 84, 78, 255)
    sd.line([(cx, cy - S * 0.1), (cx, cy + S * 0.1)], fill=groove, width=int(S * 0.035))
    sd.line([(cx - S * 0.065, cy - S * 0.03), (cx + S * 0.065, cy - S * 0.03)], fill=groove, width=int(S * 0.035))
    shadow = stone.filter(ImageFilter.GaussianBlur(S * 0.02))
    img = Image.alpha_composite(img, Image.composite(Image.new('RGBA', (S, S), (0, 0, 0, 170)), layer(), shadow.split()[3]))
    img = Image.alpha_composite(img, stone)
    return img


def main():
    big = icon()
    os.makedirs(os.path.join(ROOT, 'res'), exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, 'res', 'graveprix.ico'), sizes=[(s, s) for s in sizes])
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, 'res', 'graveprix.png'))
    buf = io.BytesIO()
    big.resize((128, 128), Image.LANCZOS).save(buf, 'PNG', optimize=True)
    data = buf.getvalue()
    lines = ['/**', ' * @file IconPng.h', " * @brief The program's icon as a 128 px PNG (tools/make_icon.py writes it).",
             ' * @author Rene Weller', ' */', '#pragma once', '', 'inline constexpr unsigned char IconPng[] = {']
    for i in range(0, len(data), 20):
        lines.append('    ' + ', '.join('0x%02x' % b for b in data[i:i + 20]) + ',')
    lines += ['};', '']
    open(os.path.join(ROOT, 'src', 'IconPng.h'), 'w', newline='\n').write('\n'.join(lines))
    with open(os.path.join(ROOT, 'res', 'graveprix.rc'), 'w', newline='\n') as f:
        f.write('// The program icon (tools/make_icon.py draws it).\nIDI_ICON1 ICON "graveprix.ico"\n')
    print('icon written')


if __name__ == '__main__':
    main()
