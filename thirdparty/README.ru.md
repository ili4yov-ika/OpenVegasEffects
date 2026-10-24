# Сторонний код

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Библиотеки и заголовки, хранящиеся рядом с кодом.

| Папка | Что это | Для чего |
|-------|---------|----------|
| [`vlc/`](vlc/) | Заголовки и исходники VLC | API libVLC, который медиаслой загружает во время выполнения (`include/`); в `runtime/<ОС>` может лежать подготовленная среда выполнения для упаковки. Исходники не собираются |
| [`openexr/`](openexr/) | OpenEXR | Экспорт кадров `.exr`, только с `-DOPENVEGAS_WITH_OPENEXR=ON` |
| [`alembic/`](alembic/) | Alembic | Импорт моделей `.abc`, только с `-DOPENVEGAS_WITH_ALEMBIC=ON` |
| `openfx/` | Стандарт OpenFX (git-подмодуль) | Только справочно; не собирается |
| `kdenlive/` | Исходники Kdenlive | Локальный справочный материал, в репозиторий не входит (`.gitignore`) |

У каждой библиотеки в её папке свой файл лицензии. Оговорки о зависимости OpenEXR
от Imath - в основном [`README.ru.md`](../README.ru.md).
