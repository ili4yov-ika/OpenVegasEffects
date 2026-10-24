#!/usr/bin/env python3
"""Regenerate the application icon, resources/icons/logo.ico, from logo.svg.

Usage: python tools/svg_to_ico.py

logo.svg has circles and rounded rectangles, which make_file_icon.py's
polygon rasteriser does not draw, so it is rasterised by Inkscape (on PATH
or in its default Windows folders) at 256x256 and Pillow writes the
multi-size .ico. The .vegfx file icon has its
own script, make_file_icon.py, which needs no Inkscape.
"""
import os
import subprocess
import sys


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Inkscape -> 256x256 PNG -> ICO.
    project_root = os.path.dirname(script_dir)
    svg_path = os.path.join(project_root, "resources", "icons", "logo.svg")
    ico_path = os.path.join(project_root, "resources", "icons", "logo.ico")
    if not os.path.exists(svg_path):
        print(f"Error: {svg_path} not found")
        return 1
    try:
        from PIL import Image
    except ImportError:
        print("Install: pip install pillow")
        return 1

    # "inkscape" on PATH is tried by running it; the install folders by
    # looking for the executable.
    inkscape = None
    for path in [
        "inkscape",
        r"C:\Program Files\Inkscape\bin\inkscape.exe",
        r"C:\Program Files (x86)\Inkscape\bin\inkscape.exe",
    ]:
        if path == "inkscape":
            try:
                subprocess.run([path, "--version"], capture_output=True, check=True)
                inkscape = path
                break
            except (subprocess.CalledProcessError, FileNotFoundError):
                continue
        elif os.path.isfile(path):
            inkscape = path
            break
    if not inkscape:
        print("Inkscape not found.")
        return 1

    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
        png_path = tmp.name
    try:
        subprocess.run(
            [inkscape, svg_path, "-w", "256", "-h", "256", "-o", png_path],
            capture_output=True,
            check=True,
        )
        img = Image.open(png_path).convert("RGBA")
    finally:
        os.unlink(png_path)
    # The sizes Windows picks from for title bars, the taskbar and Explorer.
    sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save(ico_path, format="ICO", sizes=sizes)
    print(f"Created {ico_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
