#!/usr/bin/env python3
"""Build the plug-in catalogues translations/openvegaseffects_plugins_<lang>.ts.

The reference runs every plug-in's own strings - its name, category, control
labels, units, combo items, groups and the messages it builds at run time -
through Qt translation under the context "Effect_<ShortName>"
(PluginFile::ReadPluginMetadata, PluginHostAPI::CreateFloatSlider and the
other Create* services). Its catalogues carry those contexts next to the
application's; the plug-ins' strings are not in this project's sources, so
lupdate cannot collect them and they live in catalogues of their own.

The contexts and source strings come from the reference catalogues
(VegasEffects_<lang>.qm). Japanese and Simplified Chinese take the reference's
translations; Russian, which the reference does not ship, keeps what the
existing catalogue already has and takes new translations from a JSON
dictionary {source: translation}. Messages still without one are written
unfinished, which tools/validate_translations.py reports.

  python tools/plugin_translations.py [--reference DIR] [--ru-dictionary FILE]
"""
import argparse
import json
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
LANGUAGES = {'ja': 'ja_JP', 'zh_CN': 'zh_CN', 'ru': 'ru_RU'}

# The reference's translations that lost the source's line breaks, with them
# put back at the sentence boundaries.
_CRAWL = ("Text is automatically justified on the left and right.\n"
          "Each line is formatted as a new paragraph, so only enter a line return if you want a "
          "paragraph break.\n"
          "For authenticity we recommend using a font such as 'Star Vader' or a custom image for the "
          "Movie title.\n"
          "Formatting can be further customized in the effect's properties....")
_CRAWL_CLICK = "Click the 'A' symbol to edit the text of your crawl. " + _CRAWL
_CRAWL_JA = ('テキストは左右に自動的に揃えられます。\n'
             'それぞれの行が新しいパラグラフとして構成されます。パラグラフを分けたい場合、改行を入力してください。\n'
             'ムービータイトルには、「Star Vader」のようなフォントか、カスタム画像を使用することをお勧めしています。\n'
             'フォーマットはエフェクトプロパティで細かくカスタムすることができます...')
_CRAWL_ZH = ('文本自动左右对齐。\n'
             '每行都被格式化为一个新的段落，因此，如果您想换一个段落，请仅输入换行符。\n'
             '为了真实起见，我们建议为电影标题使用诸如“ Star Vader”之类的字体或自定义图片。\n'
             '可以在效果的属性中进一步自定义格式。...')
OVERRIDES = {
    ('ja', 'Effect_StarWarsTextCrawl', _CRAWL): _CRAWL_JA,
    ('ja', 'Effect_StarWarsTextCrawl', _CRAWL_CLICK): "'A'マークをクリックしてクロールのテキストを編集してください。" + _CRAWL_JA,
    ('zh_CN', 'Effect_StarWarsTextCrawl', _CRAWL): _CRAWL_ZH,
    ('zh_CN', 'Effect_StarWarsTextCrawl', _CRAWL_CLICK): '点击“ A”符号以编辑您的抓取文本。' + _CRAWL_ZH,
    ('zh_CN', 'Effect_StarWarsTextCrawl', 'MOVIE\nTITLE'): '电影\n标题',
}


def qm_messages(path):
    """(context, source, translation) of a compiled Qt catalogue."""
    data = path.read_bytes()
    pos = 16
    while pos < len(data):
        tag = data[pos]
        length = struct.unpack('>I', data[pos + 1:pos + 5])[0]
        body = data[pos + 5:pos + 5 + length]
        pos += 5 + length
        if tag != 0x69:   # the messages block
            continue
        i, message = 0, {}
        while i < len(body):
            kind = body[i]
            i += 1
            if kind == 1:   # end of a message
                yield message.get('ctx', ''), message.get('src', ''), message.get('tr')
                message = {}
            elif kind == 3:   # translation, UTF-16
                n = struct.unpack('>I', body[i:i + 4])[0]
                i += 4
                if n != 0xffffffff:
                    message.setdefault('tr', body[i:i + n].decode('utf-16-be'))
                    i += n
            elif kind == 5:   # hash
                i += 4
            elif kind in (6, 7, 8):   # source, context, comment
                n = struct.unpack('>I', body[i:i + 4])[0]
                i += 4
                message[{6: 'src', 7: 'ctx', 8: 'cmt'}[kind]] = body[i:i + n].decode('utf-8')
                i += n
            elif kind == 2:
                continue
            else:
                break


def existing(path):
    """Finished translations of a catalogue, by (context, source) and by source."""
    by_key, by_source = {}, {}
    if not path.exists():
        return by_key, by_source
    for context in ET.parse(path).getroot().findall('context'):
        name = context.findtext('name')
        for message in context.findall('message'):
            translation = message.find('translation')
            if translation is None or translation.get('type') or not translation.text:
                continue
            source = message.findtext('source', '')
            by_key[(name, source)] = translation.text
            by_source.setdefault(source, translation.text)
    return by_key, by_source


def write(path, language, contexts):
    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<!DOCTYPE TS>',
             f'<TS version="2.1" language="{language}">']
    for name in sorted(contexts):
        lines += ['<context>', f'    <name>{escape(name)}</name>']
        for source, translation in sorted(contexts[name].items()):
            lines += ['    <message>', f'        <source>{escape(source)}</source>']
            if translation is None:
                lines.append('        <translation type="unfinished"></translation>')
            else:
                lines.append(f'        <translation>{escape(translation)}</translation>')
            lines.append('    </message>')
        lines.append('</context>')
    lines.append('</TS>')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path,
                        default=ROOT / 'SAMPLES' / 'VEGAS_Effects' / 'Translations')
    parser.add_argument('--ru-dictionary', type=Path)
    args = parser.parse_args()

    reference = {}
    for language in ('ja', 'zh_CN'):
        catalogue = args.reference / language / f'VegasEffects_{language}.qm'
        if not catalogue.exists():
            sys.exit(f'{catalogue} not found')
        reference[language] = {(c, s): t for c, s, t in qm_messages(catalogue)
                               if c.startswith('Effect_') and s}
    keys = sorted(set(reference['ja']) | set(reference['zh_CN']))

    for language in ('ja', 'zh_CN'):
        contexts = {}
        for context, source in keys:
            translation = OVERRIDES.get((language, context, source)) \
                or reference[language].get((context, source))
            contexts.setdefault(context, {})[source] = translation if translation else source
        write(ROOT / 'translations' / f'openvegaseffects_plugins_{language}.ts',
              LANGUAGES[language], contexts)

    ru_path = ROOT / 'translations' / 'openvegaseffects_plugins_ru.ts'
    by_key, by_source = existing(ru_path)
    dictionary = json.loads(args.ru_dictionary.read_text(encoding='utf-8')) if args.ru_dictionary else {}
    contexts, missing = {}, set()
    for context, source in keys:
        translation = by_key.get((context, source)) or dictionary.get(source) or by_source.get(source)
        if translation is None:
            missing.add(source)
        contexts.setdefault(context, {})[source] = translation
    write(ru_path, LANGUAGES['ru'], contexts)
    print(f'{len(keys)} messages in {len(contexts)} contexts; '
          f'Russian: {len(missing)} sources still untranslated')


if __name__ == '__main__':
    main()
