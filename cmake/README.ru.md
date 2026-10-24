# Вспомогательные скрипты CMake

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Скрипты, которые использует основной [`CMakeLists.txt`](../CMakeLists.txt).

| Файл | Что делает |
|------|------------|
| [`DeployVlc.cmake`](DeployVlc.cmake) | Запускается после каждой сборки (`cmake -P`): копирует среду выполнения libVLC - библиотеки, `plugins/` с модулями декодеров и захвата, `plugins.dat` и файлы лицензий - из `OPENVEGAS_VLC_RUNTIME_DIR` в `vlc/` рядом с исполняемым файлом |
| [`msvc_environment.cmd.in`](msvc_environment.cmd.in) | Шаблон `msvc_environment.cmd`, через который Ninja выполняет команды компиляции и компоновки MSVC: переключает консоль в UTF-8 (чтобы локализованные строки `/showIncludes` совпадали с префиксом CMake) и загружает окружение той Visual Studio, которой принадлежит выбранный `cl.exe`, чтобы не смешивались заголовки разных установок |
