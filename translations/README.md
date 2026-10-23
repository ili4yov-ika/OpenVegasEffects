# Interface translations

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

The catalogs `openvegaseffects_ru.ts`, `openvegaseffects_ja.ts` and
`openvegaseffects_zh_CN.ts` hold the Russian, Japanese and Simplified Chinese
translations. The `.qm` files are built and embedded into the application by CMake or
qmake.

After adding translatable strings, run in an environment with Qt and a compiler:

```powershell
lupdate OpenVegasEffects.pro -no-obsolete
python tools/validate_translations.py
```

Fill in the new strings before building. Keep the placeholders `%1` and `%n`, line
breaks, HTML links and file patterns `(*.ext)`. Russian messages with `%n` need three
forms, Japanese and Chinese ones a single form. Mark resource names and style keys in
`.ui` files with `notr="true"`.

Text the interface shows has to go through `tr()` (or `QT_TRANSLATE_NOOP` and
`QCoreApplication::translate` for lists kept as English keys, such as the Options
categories or the blend modes). A string shown directly never reaches the catalogs,
and `validate_translations.py` cannot see it: the tests
`optionsShowNoEnglishInOtherLanguages` and `panelsShowNoEnglishInOtherLanguages` open
Options and the panels in each language and fail on any text still showing an English
word.

## Plugin strings

`openvegaseffects_plugins_<language>.ts` hold the native plugins' own strings in
`Effect_<ShortName>` contexts, as in the reference: name, category, control labels,
units, list items, groups, search keywords and the messages the modules build while
running. These strings are not in the sources, so lupdate does not update these
catalogs: they are only compiled and embedded (`qt_add_lrelease` in CMake,
`EXTRA_TRANSLATIONS` in qmake) and loaded by a second translator
(`app::Translations::install`).

The catalogs are written by `tools/plugin_translations.py`:

```powershell
python tools/plugin_translations.py [--ru-dictionary ru.json]
```

The contexts and source strings, as well as the Japanese and Chinese translations, come
from the reference's catalogs
(`SAMPLES/VEGAS_Effects/Translations/<language>/VegasEffects_<language>.qm`). Three
translations where the reference lost line breaks are fixed in the script. The
reference has no Russian catalog: the script keeps the translations already in
`openvegaseffects_plugins_ru.ts` and adds new ones from a JSON dictionary
`{source string: translation}`. Strings without a translation are written as
unfinished, and `validate_translations.py` reports them. The search keywords in the
Russian catalog hold both Russian and English words, so search works in both
languages.

Checking the compiled catalogs in a configured test build:

```powershell
cmake --build build/ci-local-test --target translation_regression
ctest --test-dir build/ci-local-test -R translation_regression --output-on-failure
```

The test checks that all three languages load, the return to English, the contexts of
the color picker dialog and of the splash statuses, and the plural forms.
