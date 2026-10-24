# Resources

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

What the application embeds or installs besides its code.

| Path | What it holds |
|------|---------------|
| [`icons.qrc`](icons.qrc) | The Qt resource file of all icons, embedded as `:/icons/...` |
| [`icons/`](icons/) | The icons: the program's own SVGs (`toolbar/`, `text/`, `timeline/`), the reference's PNGs copied unchanged (`layout/` - see its README - and `viewer/`), the application logo (`logo.svg`, `logo.png`, `logo.ico`), the `.vegfx` file icon (`iconfile.svg`, `iconfile.ico`) and the splash screen (`splash.svg`) |
| [`OpenVegasEffects.rc`](OpenVegasEffects.rc) | The Windows resource script: the executable's icon (icon index 0) and the `.vegfx` file icon (icon index 1, used by the file association) |
| [`macos/Info.plist`](macos/Info.plist) | The bundle description for a macOS build |

`logo.ico` is made from `logo.svg` by `tools/svg_to_ico.py`, `iconfile.ico` from
`iconfile.svg` by `tools/make_file_icon.py`.
