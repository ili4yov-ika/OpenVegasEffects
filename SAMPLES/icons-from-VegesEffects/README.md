# Иконки VEGAS Effects (VegasEffects.exe) — каталог и описание

## 1. Источник и метод извлечения

- **Файл:** `SAMPLES\VEGAS_Effects\VegasEffects.exe` (23 147 544 байт, x64, Qt5, MSVC).
- **PNG:** извлечены побайтным разбором бинарника по сигнатурам `89 50 4E 47` (IHDR/IDAT/IEND), границы каждого файла определены по chunk-структуре PNG. Дедупликация — по SHA-1.
- **Имена ресурсов:** получены из UTF-16 пула имён в секции данных бинарника (это внутренний каталог иконок приложения: каждая запись — имя файла ресурса, отсортированные по алфавиту).
- Сигнатурный проход нашёл **3379 уникальных PNG**. Последующий разбор штатного Qt RCC-дерева восстановил **3380 именованных PNG** и два файла Style; разница объясняется ресурсными алиасами, при которых несколько имён используют один PNG. Старый UTF-16 regex-инвентарь пропускал часть записей и больше не используется для сопоставления.

Все извлечённые файлы лежат в `png\`, список — в `manifest_png.tsv`, весь каталог имён — в `icons_inventory.txt`.

Точное сопоставление восстановлено из встроенного Qt RCC-дерева командой
`python _tools/map_vegas_icons.py`. Именованные ресурсы находятся в
`named_png/images/images/`, а `named_manifest.tsv` связывает штатный путь,
смещение и SHA-1. Исходная папка `png/` оставлена как неизменяемый набор
дедуплицированных блобов: разные штатные имена иногда ссылаются на один PNG,
поэтому простое переименование теряло бы алиасы.

## 2. Состав (размеры изображений)

| Размер | Кол-во | Примечание |
|-------:|-------:|-----------|
| 16×16  | 1409  | базовый размер иконки 1x |
| 32×32  | 1577  | вариант @2x (HiDPI) + часть самостоятельных |
| 64×64  | 186   | крупные версии / материал |
| 128×128| 16    | крупные версии |
| 20×20, 10×10, 40×15, 80×30, 32×64, 64×32, 10×26, 5×13, 38×16, 76×32, 206×32 | 79 | служебные (каретки, слайдеры, полосы) |
| 106×60…1240×795 и пр. | 112 | водяные знаки, сплэш, баннеры, картинки-заглушки |

## 3. Схема именования (это и есть «что изображено»)

Имя ресурса описывает иконку по шаблону:

```
[тема] <имя-действия> [-<состояние>] [@2x] .png
```

- **Тема (префикс):**
  - `base` — основной набор (2549 имён);
  - `G...` — альтернативный стиль/тема (410 имён, напр. `Gundo`, `Gmask-square`);
  - `'...` (апостроф) — ещё один альтернативный стиль (415 имён, напр. `'file-video`, `'text-bottom-indent`).
- **Состояние (суффикс):**
  - без суффикса — обычное состояние (819);
  - `-hover` — наведение мыши (837);
  - `-checked` — зажатый/активированный (837);
  - `-disabled` — недоступный (836);
  - `-menu` — пункт меню (31);
  - `-invalid` — недопустимый (12);
  - `-active` — активный (2).
- **Размер:** `@2x` — HiDPI-вариант (1671 имя), без него — базовая (1703).
- Итого уникальных пар «тема+имя» — **1288**; уникальных базовых имён в основном наборе — **727**.

## 4. Категории иконок (по содержимому)

Ниже — основные функциональные семьи; в скобках количество вхождений в каталог
(с учётом всех тем/состояний/размеров).

| Категория | Кол-во | Что изображено (типичные имена) |
|-----------|-------:|--------------------------------|
| `mask*` / `gmask-` / `'mask` | 158+56+22 | Инструменты масок: прямоугольник, эллипс, звезда, полигон, добавление/перемещение/удаление точек, изогнутые пути, зеркальные ручки |
| `cursor-*` / `gcursor` | 108+52 | Курсоры редактора: ripple, roll, slide, slip, slice, stretch, hand, pen, resize-left/right, track-backward/forward |
| `text*` / `gtext` / `'text` | 92+28+22 | Настройки текста: выравнивание, отступы, регистр (uppercase/lowercase/capitalize), под-/надстрочные, межбуквенный интервал, цвет заливки/обводки |
| `file-*` / `gfile` / `'file` | 85+24+17 | Типы файлов и слоёв: video, audio, picture, 3d, offline, transition, folder, new-composite |
| `layout-*` / `glayout` / `'layout` | 82+26+19 | Раскладки окон и панелей: canvas-left/right/top/bottom/middle, «1…n» ячеек, распределение |
| `key-frame*` / `gkey` | 70+26 | Ключевые кадры: prev/next, ease-in/out, easy-ease, hold, linear, on/off, frame-in/out, треки-объекты |
| `export-*` / `gexport` | 47+23 | Экспорт: queue, preset, content, canvas, tidy, hot-folder, layered |
| `camera-*` | 42 | Камера: dolly, orbit, pan, zoom, 3D-просмотр, light |
| `checkbox-*` / `gcheckbox` / `radio-*` | 38+16+16 | Чекбоксы и радио в состояниях normal/hover/checked/disabled, mixed |
| `effect-*` / `geffect` | 37+12 | Категории эффектов: 2d, 3d, ae-plugin, behavior, geometry, transition |
| `distribute-*` / `align-*` / `galign` | 36+30+14 | Выравнивание/распределение объектов: left/right/top/bottom/center, hor/vert |
| `frame-*` / `gframe` | 30+14 | Кадры и транспорт: in/out, next/last, prev, ramp |
| `dialog-*` / `gdialog` | 23 | Иконки диалогов: critical, warning, question, information |
| `action-*` | 22 | Действия: copy, ignore, link, move |
| `folder-*` | 22 | Папки: open, locked, add-export, horizontal/vertical |
| `hud-*` | 22 | Элементы HUD: move, textbox-overflow, toolbar |
| `font-*` | 21 | Шрифт: color, outline-color/size, size, line-height |
| `caret-*` | 20 | Каретки: up/down/left/right, black/gray, menu |
| `track-*` | 19 | Дорожки: select-forward/backward, ripple, timeline |
| `graph-*` / `animation-graph` | 18 | Графики/анимация: curve-up/down, value graph, check w/ graph |
| `margin-*` | 17 | Поля (margin): left/right/top/bottom |
| `smooth-*` | 16 | Сглаживание: smooth-in/out/auto, ease |
| `clipboard-*` | 15 | Буфер обмена: copy, cut, paste |
| `play*` / `pause` / `stop-ram` / `play-ram` | 14+ | Транспорт: play, play-backwards, pause, play-ram, stop-ram |
| `zoom-*` | 14 | Зум: min, max, fit, histogram zoom |
| `browser-*` / `new-*` / `import-*` / `preset-*` | 13+ | Обзор эффектов, создание, импорт, пресеты |
| `proxy-*` / `gproxy` | 13 | Прокси-файлы: complete, queue, pause, generate, hot-folder |
| `sound-*` | 13 | Звук, аудио-слои |
| `brush-*` / `freehand-*` | 12+12 | Кисть, свободная форма (freehand-path, brush-remove) |
| `letter-*` | 12 | Направляющие буквенные маркеры scaling-horizontal/vertical |
| `vegas*` | 12 | Фирменные иконки VEGAS Effects |
| прочие | | pointer, help, pipette, batch, ripple-menu, slice, straighten, stretch, auto, key-frame, blank, white-checked, global, missing, etc. |

## 5. Явные Qt-пути иконок `:/images/images/...`

В коде программы иконки дополнительно запрашиваются напрямую по этим QIcon-путям:

```
:/images/images/clipboard-copy.png        :/images/images/clipboard-paste.png
:/images/images/clipboard-cut.png         :/images/images/cog-properties-information.png
:/images/images/exclamation.png           :/images/images/folder-open.png
:/images/images/name-creator.png          :/images/images/name-pro.png
:/images/images/name.png                  :/images/images/redo.png
:/images/images/save.png                  :/images/images/tier-lock-hover.png
:/images/images/undo.png                  :/images/images/vegas-effects-new-file.png
```

## 6. Соотношение с иконками нашей реимплементации

Наш проект хранит иконки как SVG в `resources\icons\` (114 файлов).
По базовым именам **49 имён совпадают** с каталогом референса
(например `align-left`, `distribute-top`, `mirror-horizontal`, `fill`, `gift`,
`linked`, `locked`, `auto`…). Часть наших имён — собственные соглашения
(`kf-*` — ключевые кадры, `tool-*` — инструменты, `effect-*` — категории эффектов),
а в референсе есть большой пласт имён, которых у нас пока нет
(`mask-*`, `cursor-*`, `gtext-*`, `font-*`, `hud-*`, `caret-*` и т.д.) —
их удобно использовать как основу для расширения набора.

## 7. Замечание о способе опознания

Имена и PNG теперь связаны напрямую через узлы встроенного Qt RCC-дерева
(`qRegisterResourceData`: tree `0x1412792f0`, names `0x14128b5f0`, data
`0x140d1d850`). Это авторитетное соответствие самой программы, включая темы,
состояния, HiDPI-варианты и алиасы. Для обзорной проверки также сохранены
контактные листы.

## 8. Состав папки `SAMPLES\icons-VegesEffects\`

```
png\               3379 извлечённых PNG (png_<offset>_<W>x<H>.png)
named_png\         ресурсы под точными именами из Qt RCC-дерева
manifest_png.tsv   sha1 / offset / length / width / height / file
named_manifest.tsv штатное имя / файл / offset / размер / SHA-1
icons_inventory.txt полный алфавитный каталог имён (3374 записи)
contact16\         8 контактных листов всех 16×16 иконок (увеличены ×3)
contact32\         8 контактных листов всех 32×32 иконок (увеличены ×2)
contact64\         1 лист всех 64×64
contact_misc\      4 листа прочих размеров (водяные знаки, баннеры, слайдеры)
README.md          этот файл
```
