#!/usr/bin/env python3
"""Convert / regenerate OpenVegas app icons (logo.ico + PNG).

Prefer tools/make_app_icon.py (Pillow, no Inkscape). This script keeps the
old Inkscape path as a fallback when make_app_icon cannot run.
"""
import os
import subprocess
import sys


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    make_icon = os.path.join(script_dir, "make_app_icon.py")
    try:
        r = subprocess.run([sys.executable, make_icon], check=False)
        if r.returncode == 0:
            return 0
    except OSError:
        pass

    # Fallback: Inkscape → PNG → ICO
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
        print("make_app_icon.py failed and Inkscape not found.")
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
    sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save(ico_path, format="ICO", sizes=sizes)
    print(f"Created {ico_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
