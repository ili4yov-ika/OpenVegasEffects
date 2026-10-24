# Ресурсы

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Что приложение встраивает или устанавливает помимо кода.

| Путь | Что в нём |
|------|-----------|
| [`icons.qrc`](icons.qrc) | Файл ресурсов Qt со всеми иконками, встроенными как `:/icons/...` |
| [`icons/`](icons/) | Иконки: собственные SVG программы (`toolbar/`, `text/`, `timeline/`), PNG эталона без изменений (`layout/` и `media/` - см. их README - и `viewer/`), логотип приложения (`logo.svg`, `logo.png`, `logo.ico`), иконка файлов `.vegfx` (`iconfile.svg`, `iconfile.ico`) и заставка (`splash.svg`) |
| [`OpenVegasEffects.rc`](OpenVegasEffects.rc) | Скрипт ресурсов Windows: иконка исполняемого файла (индекс 0) и иконка файлов `.vegfx` (индекс 1, её использует ассоциация файлов) |
| [`macos/Info.plist`](macos/Info.plist) | Описание бандла для сборки под macOS |

`logo.ico` делается из `logo.svg` скриптом `tools/svg_to_ico.py`, `iconfile.ico` из
`iconfile.svg` - скриптом `tools/make_file_icon.py`.
