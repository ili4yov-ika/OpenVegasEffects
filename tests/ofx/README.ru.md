# Тестовые плагины OpenFX

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Плагины OpenFX и песочница описания, которые собирают тесты хоста OpenFX
(см. «Плагины OpenFX» в [`../README.ru.md`](../README.ru.md)).

| Файл | Что делает |
|------|------------|
| `TestOfxPlugins.cpp` | Эффекты на голом C API OpenFX: `org.openvegas.test.Gain` (фильтр) и `org.openvegas.test.Wipe` (переход) в бандле OpenVegasTest; с `OPENVEGAS_OFX_CRASH` это бандл OpenVegasCrash, единственный плагин которого падает в describe |
| `OfxDescribeSandbox.cpp` | `ofx_describe_sandbox`: то же, что делает приложение при запуске с `--ofx-describe <binary> <out.json>` |
