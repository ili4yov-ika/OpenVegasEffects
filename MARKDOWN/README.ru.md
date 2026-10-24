# Документы для разработчиков

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Планы, правила и заметки по обратной разработке. Заметки об эталонном приложении
написаны по-русски, [`ARCHITECTURE.md`](ARCHITECTURE.md) - по-английски.

| Документ | О чём |
|----------|-------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Как поведение эталона соотносится с модулями этого репозитория |
| [`CHECKLIST.md`](CHECKLIST.md) | Открытые задачи проекта |
| [`INIT.MD`](INIT.MD) | Правила разработки |
| [`RE_VegasEffects.md`](RE_VegasEffects.md) | Эталонное приложение (VegasEffects.exe): запуск, окна, панели, настройки |
| [`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md) | Что входит в дистрибутив эталона и для чего каждая библиотека |
| [`RE_Project_dll.md`](RE_Project_dll.md) | Project.dll: объектная модель - проекты, композиции, слои, ключевые кадры, формат `.vegfx` |
| [`RE_Flux_dll.md`](RE_Flux_dll.md) | Flux.dll: движок рендеринга |
| [`RE_Tannen_dll.md`](RE_Tannen_dll.md) | Tannen.dll: хост плагинов и эффектов |
| [`RE_Plugin_Engine.md`](RE_Plugin_Engine.md) | Движок нативных плагинов `.hfpl` |
| [`RE_HFPL_Plugins.md`](RE_HFPL_Plugins.md) | Модули `.hfpl`: формат файла, API и контексты хоста, что запускает порт и как это проверено; [`RE_HFPL_Plugins/hfpl_metadata.tsv`](RE_HFPL_Plugins/hfpl_metadata.tsv) перечисляет все модули |
| [`RE_3D_Camera_Rule.md`](RE_3D_Camera_Rule.md) | Правило: 3D-композиции нужна камера |
| [`RE_UI_Stubs.md`](RE_UI_Stubs.md) | Аудит заглушек интерфейса и того, как каждая закрыта |
| [`RE_wnd_Layer.md`](RE_wnd_Layer.md), [`RE_wnd_Layout.md`](RE_wnd_Layout.md), [`RE_wnd_Viewer360.md`](RE_wnd_Viewer360.md) | Панели Layer, Layout и 360 Viewer |

Адреса в заметках (`FUN_14…`, `0x18…`) - адреса эталонных бинарников в Ghidra; самих
бинарников в репозитории нет (см. [`SAMPLES/README.ru.md`](../SAMPLES/README.ru.md)).
