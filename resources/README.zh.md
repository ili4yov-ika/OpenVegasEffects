# 资源

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

应用程序在代码之外嵌入或安装的内容。

| 路径 | 内容 |
|------|------|
| [`icons.qrc`](icons.qrc) | 包含所有图标的 Qt 资源文件，以 `:/icons/...` 嵌入 |
| [`icons/`](icons/) | 图标：程序自己的 SVG (`toolbar/`、`text/`、`timeline/`)、原样复制的参考程序 PNG (`layout/` 和 `media/` - 见各自的 README - 以及 `viewer/`)、应用程序标志 (`logo.svg`、`logo.png`、`logo.ico`)、`.vegfx` 文件图标 (`iconfile.svg`、`iconfile.ico`) 和启动画面 (`splash.svg`) |
| [`OpenVegasEffects.rc`](OpenVegasEffects.rc) | Windows 资源脚本：可执行文件的图标 (索引 0) 和 `.vegfx` 文件图标 (索引 1，供文件关联使用) |
| [`macos/Info.plist`](macos/Info.plist) | macOS 构建的程序包描述 |

`logo.ico` 由 `tools/svg_to_ico.py` 从 `logo.svg` 生成，`iconfile.ico` 由
`tools/make_file_icon.py` 从 `iconfile.svg` 生成。
