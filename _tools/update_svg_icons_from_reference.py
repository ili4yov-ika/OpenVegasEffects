"""Rebuild matching OpenVegas SVG icons from the named VEGAS PNG resources.

The source artwork is deliberately pixel-oriented.  Consecutive equal-colour
pixels are emitted as SVG path rectangles, retaining the reference silhouette,
anti-aliasing and alpha while allowing Qt to scale one resource at any DPI.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from collections import defaultdict
from pathlib import Path

from PIL import Image


STATE_SUFFIX = re.compile(
    r"-(hover|checked|disabled|selected|active|menu|invalid)$", re.I
)

# Local names predate the recovered resource catalogue.  Keep the public qrc
# aliases stable while selecting the matching reference artwork.
ALIASES = {
    "copy": "clipboard-copy", "cut": "clipboard-cut", "paste": "clipboard-paste",
    "new": "new-add-plus", "open": "folder-open", "stop": "stop-ram",
    "step-back": "frame-previous", "step-fwd": "frame-next",
    "to-start": "frame-first", "mark-in": "frame-in", "mark-out": "frame-out",
    "zoom-in": "zoom-max", "zoom-out": "zoom-min", "snapshot": "screenshot-camera",
    "tool-select": "pointer", "tool-hand": "hand", "tool-slice": "cursor-slice",
    "tool-slide": "cursor-slide", "tool-ripple": "cursor-ripple",
    "tool-roll": "cursor-rolling", "tool-stretch": "cursor-stretch-right",
    "tool-track-select": "cursor-all-track-forward", "tool-slip": "cursor-slip",
    "tool-ellipse": "mask-circle", "tool-orbit": "camera-orbit",
    "tool-pen": "mask-add-point", "tool-rect": "mask-square",
    "tool-rrect": "mask-rounded-rect", "tool-polygon": "mask-polygon",
    "tool-star": "mask-star", "tool-text": "text-layer", "tool-path": "freehand-path",
    "kf-off": "key-frame-off", "kf-on": "key-frame-full",
    "kf-bezier": "key-frame-manual-bezier", "kf-smooth-in": "key-frame-ease-in",
    "kf-smooth": "key-frame-easy-ease", "kf-smooth-out": "key-frame-ease-out",
    "kf-hold": "key-frame-hold", "kf-linear": "key-frame",
    "lock-off": "lock", "lock-on": "locked",
}


def core_name(path: Path) -> str:
    return STATE_SUFFIX.sub("", path.stem.removesuffix("@2x")).lower()


def png_to_svg(source: Path, display_size: int = 20) -> str:
    image = Image.open(source).convert("RGBA")
    width, height = image.size
    paths: dict[tuple[int, int, int, int], list[str]] = defaultdict(list)
    pixels = image.load()
    for y in range(height):
        x = 0
        while x < width:
            colour = pixels[x, y]
            if colour[3] == 0:
                x += 1
                continue
            end = x + 1
            while end < width and pixels[end, y] == colour:
                end += 1
            paths[colour].append(f"M{x} {y}h{end - x}v1h-{end - x}z")
            x = end

    digest = hashlib.sha1(source.read_bytes()).hexdigest()
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{display_size}" height="{display_size}" viewBox="0 0 {width} {height}">',
        f"<!-- VEGAS Effects resource: {source.name}; SHA-1: {digest} -->",
    ]
    for (red, green, blue, alpha), geometry in sorted(paths.items()):
        opacity = "" if alpha == 255 else f' fill-opacity="{alpha / 255:.4f}"'
        lines.append(
            f'<path fill="#{red:02X}{green:02X}{blue:02X}"{opacity} '
            f'd="{"".join(geometry)}"/>'
        )
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference", type=Path,
        default=Path("SAMPLES/icons-VegesEffects/named_png/images/images"),
    )
    parser.add_argument("--icons", type=Path, default=Path("resources/icons"))
    args = parser.parse_args()

    references: dict[str, Path] = {}
    for png in args.reference.glob("*.png"):
        # Only the normal state is used by a plain SVG QIcon. Prefer @2x so
        # antialiased edges retain their detail on high-DPI displays.
        stem = png.stem
        if STATE_SUFFIX.search(stem.removesuffix("@2x")):
            continue
        key = core_name(png)
        previous = references.get(key)
        if previous is None or ("@2x" in stem and "@2x" not in previous.stem):
            references[key] = png

    changed = []
    for svg in args.icons.rglob("*.svg"):
        local_name = svg.stem.lower()
        source = references.get(ALIASES.get(local_name, local_name))
        if source is None:
            continue
        svg.write_text(png_to_svg(source), encoding="utf-8", newline="\n")
        changed.append((svg, source))

    print(f"Updated {len(changed)} SVG files from {len(set(p for _, p in changed))} resources")
    for target, source in changed:
        print(f"{target.as_posix()} <- {source.name}")


if __name__ == "__main__":
    main()
