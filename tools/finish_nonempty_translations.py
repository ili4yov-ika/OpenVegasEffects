#!/usr/bin/env python3
"""Mark filled Qt TS translations as finished without reformatting the file."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


PAIRED_TRANSLATION = re.compile(
    r'(<translation\b[^>]*?(?<!/)>)((?:(?!<translation\b).)*?)(</translation>)',
    re.DOTALL,
)
EMPTY_TRANSLATION = re.compile(r'<translation\b([^>]*)/>')
TAGS = re.compile(r"<[^>]+>")


def finish_nonempty(path: Path) -> int:
    source = path.read_text(encoding="utf-8")
    changed = 0

    def replace_pair(match: re.Match[str]) -> str:
        nonlocal changed
        opening = match.group(1)
        has_text = bool(TAGS.sub("", match.group(2)).strip())
        unfinished = 'type="unfinished"' in opening
        terminal = 'type="obsolete"' in opening or 'type="vanished"' in opening
        if has_text and unfinished:
            opening = re.sub(r'\s+type="unfinished"', "", opening, count=1)
            changed += 1
        elif not has_text and not unfinished and not terminal:
            opening = opening[:-1].rstrip() + ' type="unfinished">'
            changed += 1
        return opening + match.group(2) + match.group(3)

    def replace_empty(match: re.Match[str]) -> str:
        nonlocal changed
        attributes = match.group(1)
        if "type=" in attributes:
            return match.group(0)
        changed += 1
        return '<translation' + attributes.rstrip() + ' type="unfinished" />'

    result = PAIRED_TRANSLATION.sub(replace_pair, source)
    result = EMPTY_TRANSLATION.sub(replace_empty, result)
    if result != source:
        path.write_text(result, encoding="utf-8")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalogues", nargs="+", type=Path)
    args = parser.parse_args()
    for catalogue in args.catalogues:
        changed = finish_nonempty(catalogue)
        print(f"{catalogue}: finished {changed} non-empty translations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
