# OpenFX 测试插件

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

OpenFX 宿主的测试所构建的 OpenFX 插件和描述沙盒
（见 [`../README.zh.md`](../README.zh.md) 中的“OpenFX 插件”）。

| 文件 | 作用 |
|------|------|
| `TestOfxPlugins.cpp` | 用纯 OpenFX C API 编写的效果：OpenVegasTest 包中的 `org.openvegas.test.Gain`（滤镜）和 `org.openvegas.test.Wipe`（转场）；以 `OPENVEGAS_OFX_CRASH` 构建时是 OpenVegasCrash 包，其唯一插件在 describe 时崩溃 |
| `OfxDescribeSandbox.cpp` | `ofx_describe_sandbox`：与应用程序以 `--ofx-describe <binary> <out.json>` 运行时所做的相同 |
