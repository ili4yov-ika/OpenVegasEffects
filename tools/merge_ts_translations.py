#!/usr/bin/env python3
"""Fill a source-synchronized Qt TS catalog from an extracted reference TS.

The destination catalog defines the set of messages that belongs to this
project.  The reference may contain many more plugin strings, so only exact
context matches or source strings with one unambiguous translation are copied.

Usage: python tools/merge_ts_translations.py <destination.ts> <reference.ts>

Translations already present in the destination are kept; the file is
rewritten with ElementTree's indentation, so review the diff before
committing it.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import xml.etree.ElementTree as ET


def message_key(context: str, message: ET.Element) -> tuple[str, str, str]:
    """A message's identity: its context, source text and disambiguation."""
    return (
        context,
        message.findtext("source", default=""),
        message.findtext("comment", default=""),
    )


def translation_text(message: ET.Element) -> str:
    """The message's translation, or "" when it has none or it is dead."""
    translation = message.find("translation")
    if translation is None or translation.get("type") in {"obsolete", "vanished"}:
        return ""
    return "".join(translation.itertext()).strip()


def load_reference(path: Path):
    """Index the reference: translations by full key, and by source text.

    A source text translated differently in different contexts is left out
    of the second index - guessing between them would be wrong half the time.
    """
    root = ET.parse(path).getroot()
    exact: dict[tuple[str, str, str], str] = {}
    by_source: dict[str, set[str]] = defaultdict(set)
    for context in root.findall("context"):
        name = context.findtext("name", default="")
        for message in context.findall("message"):
            text = translation_text(message)
            source = message.findtext("source", default="")
            if not source or not text:
                continue
            exact[message_key(name, message)] = text
            by_source[source].add(text)
    unambiguous = {
        source: next(iter(values))
        for source, values in by_source.items()
        if len(values) == 1
    }
    return exact, unambiguous


def merge(destination: Path, reference: Path) -> tuple[int, int, int]:
    """Fill the destination's empty translations; returns the counts filled
    by exact key, by unambiguous source text, and in total."""
    exact, unambiguous = load_reference(reference)
    tree = ET.parse(destination)
    root = tree.getroot()
    exact_count = fallback_count = 0

    for context in root.findall("context"):
        name = context.findtext("name", default="")
        for message in context.findall("message"):
            translation = message.find("translation")
            if translation is None:
                translation = ET.SubElement(message, "translation")
            # Never overwrite a translation someone already made.
            if "".join(translation.itertext()).strip():
                continue

            source = message.findtext("source", default="")
            text = exact.get(message_key(name, message))
            if text:
                exact_count += 1
            else:
                text = unambiguous.get(source)
                if text:
                    fallback_count += 1
            if not text:
                continue

            # clear() also drops type="unfinished": the copied text is final.
            translation.clear()
            translation.text = text

    ET.indent(tree, space="    ")
    xml = ET.tostring(root, encoding="unicode")
    destination.write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + xml + "\n",
        encoding="utf-8",
    )
    return exact_count, fallback_count, exact_count + fallback_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    exact, fallback, total = merge(args.destination, args.reference)
    print(f"{args.destination}: imported {total} translations "
          f"({exact} exact-context, {fallback} unambiguous-source)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
