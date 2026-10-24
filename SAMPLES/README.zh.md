# 示例与参考资料

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

开发用的资料；这里的内容都不参与构建。

| 文件夹 | 内容 |
|--------|------|
| `VEGAS_Effects/` | 参考应用程序 (VEGAS Effects 5.0.2) 的本地副本：二进制文件、原生 `.hfpl` 插件 (`Plugins/`)、翻译、环境贴图和反编译的源码 (`decompile-src/`)。它**不在仓库中** (`SAMPLES/.gitignore`)；要运行逆向工程工具以及需要参考程序插件的测试，请把已安装的副本放在这里 |
| [`vegfx_projects/`](vegfx_projects/) | 示例项目：`project_1` (在参考程序中制作的 2D 项目)、`project_2` (3D 模型)、`project_3` (各面带有文字的立方体、关键帧旋转、所有内容都加了特效，以及两个带音频特效的音乐文件；由测试 `buildProject3` 生成) |
| [`screenshots/`](screenshots/) | 参考程序的截图及逐像素分析，是界面的依据 |
| [`icons-from-VegesEffects/`](icons-from-VegesEffects/) | 从参考程序资源中提取的图标及其目录 |

示例项目的媒体文件 (`*.mp3`、`*.mp4`、`*.png`、`*.psd`) 除非特意添加，否则会被
`vegfx_projects/.gitignore` 忽略。
