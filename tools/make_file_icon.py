#!/usr/bin/env python3
"""Build resources/icons/iconfile.ico, the icon of .vegfx project files.

The SVG holds filled polygons only (absolute M/L/H/V/Z paths), so it is
rasterised here with Pillow at 8x and scaled down - no SVG renderer needed.
The executable embeds the .ico as its second icon (index 1), which the
installer and the app's own file-type registration name in DefaultIcon.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SUPERSAMPLE = 8


def polygons(svg):
    """(fill, [(x, y), ...]) for each <path> of the SVG."""
    shapes = []
    for match in re.finditer(r'<path\s+d="([^"]+)"\s+fill="(#[0-9A-Fa-f]{6}|white|black)"', svg):
        data, fill = match.group(1), match.group(2)
        fill = {'white': '#FFFFFF', 'black': '#000000'}.get(fill, fill)
        points, x, y = [], 0.0, 0.0
        for command, args in re.findall(r'([MLHVZ])([^MLHVZ]*)', data):
            values = [float(v) for v in re.findall(r'-?\d+(?:\.\d+)?', args)]
            if command in 'ML':
                for i in range(0, len(values), 2):
                    x, y = values[i], values[i + 1]
                    points.append((x, y))
            elif command == 'H':
                for x in values:
                    points.append((x, y))
            elif command == 'V':
                for y in values:
                    points.append((x, y))
        shapes.append((fill, points))
    return shapes


def render(svg, size, view):
    scale = size * SUPERSAMPLE / view
    image = Image.new('RGBA', (size * SUPERSAMPLE, size * SUPERSAMPLE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for fill, points in polygons(svg):
        draw.polygon([(px * scale, py * scale) for px, py in points], fill=fill)
    return image.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    svg_path = os.path.join(root, 'resources', 'icons', 'iconfile.svg')
    ico_path = os.path.join(root, 'resources', 'icons', 'iconfile.ico')
    with open(svg_path, encoding='utf-8') as f:
        svg = f.read()
    view = float(re.search(r'viewBox="0 0 (\d+(?:\.\d+)?) ', svg).group(1))
    if not polygons(svg):
        print('No paths found in', svg_path)
        return 1
    images = [render(svg, size, view) for size in SIZES]
    images[-1].save(ico_path, format='ICO', sizes=[(s, s) for s in SIZES],
                    append_images=images[:-1])
    print('Created', ico_path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
