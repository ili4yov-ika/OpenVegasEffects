# OpenFX test plug-ins

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

OpenFX plug-ins and the describe sandbox that the OpenFX host's tests build
(see "OpenFX plug-ins" in [`../README.md`](../README.md)).

| File | What it does |
|------|--------------|
| `TestOfxPlugins.cpp` | Image effects written against the bare OpenFX C API: `org.openvegas.test.Gain` (a filter) and `org.openvegas.test.Wipe` (a transition) in the OpenVegasTest bundle; built with `OPENVEGAS_OFX_CRASH` it is the OpenVegasCrash bundle, whose one plug-in faults in describe |
| `OfxDescribeSandbox.cpp` | `ofx_describe_sandbox`: what the application does when run with `--ofx-describe <binary> <out.json>` |
