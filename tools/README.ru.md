# Инструменты

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Помощники для сборки, упаковки, переводов и обратной разработки.

## Сборка и упаковка

| Файл | Что делает |
|------|------------|
| [`msvc_build.cmd`](msvc_build.cmd) | Конфигурирует и собирает пресет CMake с загруженным окружением Visual Studio 2022: `msvc_build.cmd <команда> [пресет]`, где команда - `configure`, `build`, `buildall`, `probe` или `clean` |
| [`build_windows_installer.bat`](build_windows_installer.bat) | Собирает установщик Windows: Release с MSVC и Ninja, `cmake --install`, windeployqt, среда выполнения MSVC, NSIS |
| [`nsis_installer.nsi`](nsis_installer.nsi), [`nsis_payload.ps1`](nsis_payload.ps1) | Скрипт NSIS и список файлов, который он упаковывает |
| [`build_deb.sh`](build_deb.sh), [`debian/`](debian/) | Пакет `.deb` |
| [`build_rpm.sh`](build_rpm.sh), [`rpm/`](rpm/) | Пакет `.rpm` |
| [`setup_macos.sh`](setup_macos.sh), [`macos_build.sh`](macos_build.sh) | macOS: установка Qt, CMake и Ninja через Homebrew; сборка по пресетам |
| [`openvegaseffects.desktop`](openvegaseffects.desktop) | Ярлык приложения для Linux |
| [`svg_to_ico.py`](svg_to_ico.py), [`make_file_icon.py`](make_file_icon.py) | Иконка приложения и иконка файлов `.vegfx` из их SVG |

## Переводы

| Файл | Что делает |
|------|------------|
| [`validate_translations.py`](validate_translations.py) | Проверяет каждое активное сообщение каталогов `.ts`: полноту, плейсхолдеры, формы множественного числа, разметку, фильтры файлов |
| [`plugin_translations.py`](plugin_translations.py) | Пишет каталоги плагинов `openvegaseffects_plugins_<язык>.ts` по каталогам эталона |
| [`merge_ts_translations.py`](merge_ts_translations.py) | Заполняет каталог по эталонному каталогу - по контексту или по однозначной исходной строке |
| [`finish_nonempty_translations.py`](finish_nonempty_translations.py) | Помечает заполненные переводы завершёнными, не переформатируя файл |
| [`normalize_ui_double_properties.py`](normalize_ui_double_properties.py) | Нормализует дробные свойства `QDoubleSpinBox` в формах `.ui` |

## Обратная разработка

| Файл | Что делает |
|------|------------|
| [`hfpl_runtime_probe.cpp`](hfpl_runtime_probe.cpp) | Консольная проба нативных модулей `.hfpl` (цель `hfpl_runtime_probe`, `msvc_build.cmd probe`): загружает модули, запускает их в разных контекстах хоста и сообщает, что они читают и рисуют |
| `*Reference.java`, [`LayerStrings.java`](LayerStrings.java) | Скрипты Ghidra, которые только читают эталон и извлекают его построители и таблицы (Controls, Options, Layer, Timeline, 360 Viewer, Behaviors) для заметок в [`MARKDOWN/`](../MARKDOWN/) |
