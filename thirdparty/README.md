# Third-party code

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Libraries and headers kept next to the code.

| Folder | What it is | Used for |
|--------|-----------|----------|
| [`vlc/`](vlc/) | VLC's headers and sources | The libVLC API the media layer loads at run time (`include/`); `runtime/<OS>` may hold a prepared runtime for packaging. The sources are not built |
| [`openexr/`](openexr/) | OpenEXR | `.exr` frame export, only with `-DOPENVEGAS_WITH_OPENEXR=ON` |
| [`alembic/`](alembic/) | Alembic | `.abc` model import, only with `-DOPENVEGAS_WITH_ALEMBIC=ON` |
| `openfx/` | The OpenFX standard (a git submodule) | Reference only; not built |
| `kdenlive/` | Kdenlive's sources | A local reference, not in the repository (`.gitignore`) |

Each library keeps its own license file in its folder. The caveats about OpenEXR's
Imath dependency are in the main [`README.md`](../README.md).
