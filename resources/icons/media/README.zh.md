# VEGAS Effects 媒体预览图标

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

这些 PNG 是从 `SAMPLES/icons-from-VegesEffects/named_png/images/images/` 原样复制的，取自原始 RCC 资源：参考程序的媒体面板在没有图片时显示的带边框 106×60（@2x 212×120）预览。`resources/icons.qrc` 将它们打包在 `:/icons/media/` 下；`MediaPanel.cpp` 在读取视频帧时显示视频图标，对没有图片的项目显示音频、3D 模型、3D 模型动画、合成镜头和离线（文件缺失）图标。
