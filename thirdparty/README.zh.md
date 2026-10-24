# 第三方代码

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

与代码放在一起的库和头文件。

| 文件夹 | 内容 | 用途 |
|--------|------|------|
| [`vlc/`](vlc/) | VLC 的头文件和源码 | 媒体层在运行时加载的 libVLC API (`include/`)；`runtime/<OS>` 中可以放置为打包准备的运行时。源码不参与构建 |
| [`openexr/`](openexr/) | OpenEXR | 导出 `.exr` 帧，仅在 `-DOPENVEGAS_WITH_OPENEXR=ON` 时启用 |
| [`alembic/`](alembic/) | Alembic | 导入 `.abc` 模型，仅在 `-DOPENVEGAS_WITH_ALEMBIC=ON` 时启用 |
| `openfx/` | OpenFX 标准 (git 子模块) | 仅供参考；不参与构建 |
| `kdenlive/` | Kdenlive 源码 | 本地参考资料，不在仓库中 (`.gitignore`) |

每个库在其文件夹中都有自己的许可证文件。关于 OpenEXR 依赖 Imath 的注意事项见主
[`README.zh.md`](../README.zh.md)。
