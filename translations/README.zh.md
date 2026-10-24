# 界面翻译

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

翻译目录 `openvegaseffects_ru.ts`、`openvegaseffects_ja.ts` 和
`openvegaseffects_zh_CN.ts` 包含俄语、日语和简体中文翻译。`.qm` 由 CMake 或 qmake 构建
并嵌入应用程序。

添加可翻译字符串后，请在带有 Qt 和编译器的环境中运行：

```powershell
lupdate OpenVegasEffects.pro -no-obsolete
python tools/validate_translations.py
```

构建前请填写新字符串。保留占位符 `%1`、`%n`、换行、HTML 链接和文件模式 `(*.ext)`。
带 `%n` 的俄语消息需要三种形式，日语和中文只需一种。`.ui` 中的资源名称和样式键请标记
`notr="true"`。

界面显示的文本必须经过 `tr()`（对于以英文键保存的列表，例如选项分类或混合模式，使用
`QT_TRANSLATE_NOOP` 和 `QCoreApplication::translate`）。直接显示的字符串不会进入翻译
目录，`validate_translations.py` 也看不到它：测试 `optionsShowNoEnglishInOtherLanguages`
和 `panelsShowNoEnglishInOtherLanguages` 会以每种语言打开选项和各面板，只要有文本仍显示
英文单词就会失败。

## 插件字符串

`openvegaseffects_plugins_<语言>.ts` 在与参考程序相同的 `Effect_<ShortName>` 上下文中
保存原生插件自己的字符串：名称、类别、控件标签、单位、列表项、分组、搜索关键词，以及模块
运行时生成的消息。这些字符串不在源代码中，因此 lupdate 不会更新这些目录：它们只会被编译
并嵌入（CMake 中为 `qt_add_lrelease`，qmake 中为 `EXTRA_TRANSLATIONS`），并由第二个翻译器
加载（`app::Translations::install`）。

这些目录由 `tools/plugin_translations.py` 生成：

```powershell
python tools/plugin_translations.py [--ru-dictionary ru.json]
```

上下文和源字符串，以及日语和中文翻译，均取自参考程序的翻译目录
（`SAMPLES/VEGAS_Effects/Translations/<语言>/VegasEffects_<语言>.qm`）。参考程序中丢失了
换行的三条翻译已在脚本中修正。参考程序没有俄语目录：脚本会保留
`openvegaseffects_plugins_ru.ts` 中已有的翻译，并从 JSON 词典 `{源字符串: 翻译}` 中添加
新的翻译。没有翻译的字符串会被写为 unfinished，`validate_translations.py` 会报告它们。
俄语目录中的搜索关键词同时包含俄语和英语单词，因此两种语言都能搜索。

在已配置的测试构建中检查编译后的目录：

```powershell
cmake --build build/Windows_MSVC-x64-Release-Tests --target translation_regression
ctest --test-dir build/Windows_MSVC-x64-Release-Tests -R translation_regression --output-on-failure
```

该测试检查三种语言是否都能加载、能否切回英语、颜色选择对话框和启动画面状态的上下文，
以及复数形式。
