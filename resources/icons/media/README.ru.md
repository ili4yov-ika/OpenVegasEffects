# Значки превью панели «Медиа» VEGAS Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Эти PNG — неизменённые копии из `SAMPLES/icons-from-VegesEffects/named_png/images/images/`, извлечённые из исходных ресурсов RCC: обрамлённые превью 106×60 (@2x 212×120), которые панель «Медиа» эталона показывает вместо картинки. `resources/icons.qrc` упаковывает их под `:/icons/media/`; `MediaPanel.cpp` показывает видеозначок, пока читается кадр видео, а значки звука, 3D-модели, анимации 3D-модели, композитного плана и отсутствующего файла — для того, у чего картинки нет.
