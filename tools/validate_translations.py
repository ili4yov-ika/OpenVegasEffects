#!/usr/bin/env python3
"""Check active Qt TS messages: completeness, placeholders, plurals and markup.

Run from any directory: python tools/validate_translations.py
Exit nonzero if a catalogue is incomplete or structurally unsafe to release.
"""
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


class Markup(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.tags = []
        self.links = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        self.tags.append(('start', tag))
        for name, value in attrs:
            if name in ('href', 'src'):
                self.links.append((name, value))

    def handle_endtag(self, tag):
        self.tags.append(('end', tag))


def validate(path):
    root = ET.parse(path).getroot()
    plural_count = 3 if root.get('language', '').startswith('ru') else 1
    errors = []
    active = 0
    for context in root.findall('context'):
        for message in context.findall('message'):
            translation = message.find('translation')
            if translation is not None and translation.get('type') in ('obsolete', 'vanished'):
                continue
            active += 1
            source = message.findtext('source', '')
            key = f'{context.findtext("name")}: {source!r}'
            if translation is None or translation.get('type') == 'unfinished':
                errors.append(f'{key}: unfinished translation')
                continue
            forms = translation.findall('numerusform')
            if message.get('numerus') == 'yes' and len(forms) != plural_count:
                errors.append(f'{key}: expected {plural_count} plural forms')
            for form in forms or [translation]:
                text = ''.join(form.itertext())
                if not text.strip():
                    errors.append(f'{key}: empty translation')
                if '\ufffd' in text:
                    errors.append(f'{key}: Unicode replacement character')
                tokens = lambda s: Counter(re.findall(r'%L?\d+|%n', s))
                if tokens(source) != tokens(text):
                    errors.append(f'{key}: placeholder mismatch')
                if source.count('\n') != text.count('\n'):
                    errors.append(f'{key}: line-break mismatch')
                if re.search(r'</?(?:a|b|br|p|span|html|body|i)\b', source):
                    original, translated = Markup(source), Markup(text)
                    if original.tags != translated.tags or original.links != translated.links:
                        errors.append(f'{key}: markup/link mismatch')
                # File selectors must retain wildcard groups and filter separators.
                filters = lambda s: re.findall(r'\([^()]*\*[^()]*\)', s)
                if filters(source) and (filters(source) != filters(text)
                                       or source.count(';;') != text.count(';;')):
                    errors.append(f'{key}: file filter mismatch')
    print(f'{path.name}: {active} active messages, {len(errors)} errors')
    for error in errors:
        print(f'  {error}')
    return not errors


if __name__ == '__main__':
    paths = sorted((Path(__file__).resolve().parent.parent / 'translations').glob('*.ts'))
    results = [validate(path) for path in paths]
    sys.exit(0 if paths and all(results) else 1)
