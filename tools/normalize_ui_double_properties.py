#!/usr/bin/env python3
"""Normalize floating-point properties inside QDoubleSpinBox UI blocks."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


WIDGET = re.compile(
    r'(<widget\s+class="QDoubleSpinBox".*?</widget>)', re.DOTALL
)
FLOAT_PROPERTY = re.compile(
    r'(<property\s+name="(?:minimum|maximum|singleStep|value)"\s*>)'
    r'(\s*)<number>(.*?)</number>(\s*</property>)',
    re.DOTALL,
)


def normalize(path: Path) -> int:
    source = path.read_text(encoding="utf-8")
    changes = 0

    def widget(match: re.Match[str]) -> str:
        nonlocal changes

        def floating(prop: re.Match[str]) -> str:
            nonlocal changes
            changes += 1
            return (f"{prop.group(1)}{prop.group(2)}<double>{prop.group(3)}</double>"
                    f"{prop.group(4)}")

        return FLOAT_PROPERTY.sub(floating, match.group(1))

    result = WIDGET.sub(widget, source)
    if result != source:
        path.write_text(result, encoding="utf-8", newline="")
    return changes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    total = 0
    for path in args.paths:
        count = normalize(path)
        if count:
            print(f"{path}: normalized {count} floating-point properties")
            total += count
    print(f"Normalized {total} properties")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
