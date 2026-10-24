# Переводы интерфейса

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Каталоги `openvegaseffects_ru.ts`, `openvegaseffects_ja.ts` и
`openvegaseffects_zh_CN.ts` содержат русский, японский и упрощённый китайский
переводы. `.qm` собираются и встраиваются в приложение через CMake или qmake.

После добавления переводимых строк выполните в окружении Qt и компилятора:

```powershell
lupdate OpenVegasEffects.pro -no-obsolete
python tools/validate_translations.py
```

Заполните новые строки перед сборкой. Сохраняйте подстановки `%1`, `%n`,
переносы строк, HTML-ссылки и шаблоны файлов `(*.ext)`. Русские сообщения с `%n`
требуют трёх форм, японские и китайские — одной. Имена ресурсов и служебные
ключи стилей в `.ui` помечайте `notr="true"`.

Текст, который показывает интерфейс, должен проходить через `tr()` (или
`QT_TRANSLATE_NOOP` и `QCoreApplication::translate` для списков, хранящихся
английскими ключами, — разделов Настроек, режимов наложения). Строка, показанная
напрямую, не попадает в каталоги, и `validate_translations.py` её не видит: тесты
`optionsShowNoEnglishInOtherLanguages` и `panelsShowNoEnglishInOtherLanguages`
открывают Настройки и панели на каждом языке и падают на любом тексте, где осталось
английское слово.

## Строки плагинов

`openvegaseffects_plugins_<язык>.ts` — собственные строки нативных плагинов в
контекстах `Effect_<ShortName>`, как в референсе: название, категория, подписи
параметров, единицы, пункты списков, группы, ключевые слова поиска и сообщения,
которые модули строят во время работы. В исходниках этих строк нет, поэтому
lupdate эти каталоги не обновляет: они только компилируются и встраиваются
(`qt_add_lrelease` в CMake, `EXTRA_TRANSLATIONS` в qmake) и загружаются вторым
переводчиком (`app::Translations::install`).

Каталоги пишет `tools/plugin_translations.py`:

```powershell
python tools/plugin_translations.py [--ru-dictionary ru.json]
```

Контексты и исходные строки, а также японские и китайские переводы берутся из
каталогов референса (`SAMPLES/VEGAS_Effects/Translations/<язык>/VegasEffects_<язык>.qm`).
Три перевода, где референс потерял переносы строк, исправлены в скрипте. Русского
каталога у референса нет: скрипт сохраняет переводы, которые уже есть в
`openvegaseffects_plugins_ru.ts`, и добавляет новые из JSON-словаря
`{исходная строка: перевод}`. Строки без перевода записываются как unfinished,
их покажет `validate_translations.py`. Ключевые слова поиска в русском каталоге
содержат и русские, и английские слова, чтобы поиск работал на обоих языках.

Проверка скомпилированных каталогов в настроенной сборке тестов:

```powershell
cmake --build build/Windows_MSVC-x64-Release-Tests --target translation_regression
ctest --test-dir build/Windows_MSVC-x64-Release-Tests -R translation_regression --output-on-failure
```

Тест проверяет загрузку всех трёх языков, возврат к английскому, контексты
диалога выбора цвета и статусов сплеша, а также формы множественного числа.
