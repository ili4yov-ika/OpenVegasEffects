# RE: плагины `.hfpl` — формат файлов HitFilm PLugin

Разбор структуры родных плагинов эффектов референса: файл как PE-библиотека,
контракт экспортов, подпись, строковый пул и блок идентичности, встроенные
шейдеры. Данные получены прямым разбором бинарников (`PE`, таблицы импорта/
экспорта, поиск строк) без загрузки кода плагинов (`LoadLibrary` не
используется — обходные читатели на PowerShell/Java).

Дополняет [`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md)
§5.1 (соглашения загрузки через `Tannen.dll`) и
[`RE_VegasEffects.md`](RE_VegasEffects.md) (код загрузчика и связка с UI).

> **Дополнение от 5 сентября 2026.** С тех пор как этот отчёт был написан, `PluginInfo` **вызван
> у всех 321 модуля** — метаданные больше не восстанавливаются из `.rdata` наугад, а читаются
> у самого плагина. Полный дамп: [`RE_HFPL_Plugins/hfpl_metadata.tsv`](RE_HFPL_Plugins/hfpl_metadata.tsv)
> (321 строка: тип, GUID, magic, версия, флаги, идентификатор, имя, категория, вендор). Контракт и
> исправленная раскладка `PluginMetadata` — в [`RE_Plugin_Engine.md`](RE_Plugin_Engine.md) §4.3
> и §11. Разделы 7 и 11 ниже дополнены тем, что из этого следует.

---

## 1. `.hfpl` — это обычная PE-DLL x64

Все **321** файл в `SAMPLES/VEGAS_Effects/Plugins/` — PE-библиотеки x64 с
нестандартным расширением (HitFilm PLugin). Никаких собственных контейнеров:
файл читается стандартным PE-загрузчиком Windows.

| Параметр | Значение (для всех 321) |
|----------|------------------------|
| Сигнатура | `MZ`, `e_lfanew` = 0xF8 (младшие 2 байта) |
| Машина | `0x8664` (AMD64) |
| Магия PE | `0x20B` (PE32+) |
| Тип | DLL (`0x2000`) |
| Подсистема | `2` (GUI) |
| Секций | ровно **6**: `.text`, `.rdata`, `.data`, `.pdata`, `.rsrc`, `.reloc` |
| DllCharacteristics | `0x0160` — HIGH_ENTROPY_VA + DYNAMIC_BASE + NX_COMPAT |
| ImageBase | `0x180000000` (выборка) |
| TimeDateStamp | **2023-08-01 у всех** — одна сборка комплекта |
| Расширение | в PE не кодируется: имя DLL-модуля в таблице экспорта = имя файла (`'Balance.hfpl'`) |

Расширение `.hfplx` (шифрованный вариант из масок загрузчика) в поставке
**отсутствует** — все файлы открытые `.hfpl`.

---

## 2. Контракт экспортов: ровно два символа

У **всех 321** плагина в экспорт-таблице ровно два символа, в порядке
`Notify; PluginInfo`:

```
PluginInfo   // реквизит: тип, имя, категория, вендор, идентификатор
Notify       // уведомления хоста: Notify(void*, void*, int message), message 0 = PluginLoaded
```

Больше ничего не экспортируется ни одним плагином. Это согласуется с портом
загрузчика: `GetProcAddress(h, "PluginInfo")` / `GetProcAddress(h, "Notify")`,
при отсутствии любого из них — исключение «Missing plugin entry point» (см.
RE_VegasEffects.md). Проверка «Qt5Core.dll, переименованная в `.hfpl`» такой
контракт не проходит и отбрасывается — свойств плагина в файле нет, только ABI.

---

## 3. Подпись Authenticode

Все файлы подписаны код-сигнатурой FXhome. Каталог `Security` (`DataDirectory[4]`)
указывает на блоб **постоянного размера 11800 байт** у всех 321 файла — это
один и тот же сертификат+метка времени, пришитые к сборке целиком
(FXHOME LIMITED, цепочка/контрподпись GlobalSign).

Расположение — файловый конец, за секциями (для крошечных аудио-плагинов
подпись занимает заметную долю файла):

| Плагин | Размер файла | Смещение подписи | Доля подписи |
|--------|-------------:|-----------------:|-------------:|
| `Fade.hfpl` (аудио-переход) | 23576 | 0x2E00 | 50.1 % |
| `Balance.hfpl` (аудио) | 26136 | 0x3800 | 45.1 % |
| `Extrude.hfpl` (геометрия) | 55320 | 0xAA00 | 21.3 % |
| `Invert.hfpl` (2D) | 411672 | 0x61A00 | 2.9 % |
| `Text.hfpl` (2D) | 2216984 | 0x21A600 | 0.5 % |

---

## 4. Состав комплекта и GPU/CPU-профили

Общий объём `.hfpl` — **164.9 МБ**. Распределение по каталогам (реальный счёт:
`2D` = **241**, а не 242 как в §5.1 прежнего отчёта — там каталоговая строка
содержит опечатку; категорийная таблица давала верные 241):

| Каталог | Файлов | GL (`opengl32`) | CPU | Объём |
|---------|-------:|----------------:|----:|------:|
| `2D` | 241 | 241 | 0 | 143.6 МБ |
| `Behavior` | 43 | 8 | 35 | 9.8 МБ |
| `Audio` | 16 | 0 | 16 | 4.1 МБ |
| `VideoTransitions` | 15 | 15 | 0 | 6.4 МБ |
| `Geometry` | 4 | 1 | 3 | 0.86 МБ |
| `AudioTransitions` | 2 | 0 | 2 | 47 КБ |
| **Итого** | **321** | **265** | **56** | **164.9 МБ** |

Размеры: минимум 23576 Б (`Fade`, `CrossFade`), максимум **2274840 Б**
(`PulpScifiTitleCrawl.hfpl`). Крупнейшие — текстовые и трекинговые:

```
PulpScifiTitleCrawl  2274840
CreditsTextCrawl     2256920
Waveform             2249240
WaveformParade       2248728
360Text              2233880
Text                 2216984
```

---

## 5. Импорты

Базовый набор у всех: `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll`,
`api-ms-win-crt-*.dll` (runtime/string/heap/math/stdio/time/…), `KERNEL32.dll`.
Манифест-проверка: **`Qt` ни один плагин не импортирует** — UI строит хост,
плагин несёт только числовой код и строки.

Специфика по классам:

| Класс | Плагин | Доп. импорты | Что значит |
|-------|--------|--------------|------------|
| GPU-эффекты | `Invert`, `ChromaKey`, `CrossDissolve`, `Text`, … | `OPENGL32.dll` | рендер на GPU через OpenGL |
| CPU-эффекты | `Balance`, `Extrude`, `Push`, … | — | чистая математика на C++ |
| Трекинг/стабилизация | `MotionTrack`, `AutomaticStabilizer` | `opencv_world460.dll` | **OpenCV 4.6** |
| Текст | `Text.hfpl` | `USER32`, `GDI32`, `SHELL32`, `ole32`, `ADVAPI32` | работа с системой: шрифты, файлы, оболочка |

---

## 6. Ресурсы: только манифест, без VERSIONINFO

- Ни у одного проверенного плагина (выборка всех категорий) нет ресурса
  `VS_VERSION_INFO` — версия, компания и т.п. в ресурсах отсутствуют.
- `.rsrc` у всех крошечный (пример: `Balance.hfpl`, .rsrc: `@file 0x3400`,
  размер `0x200`) и содержит единственный ресурс — **RT_MANIFEST**:

```xml
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
```

Вся фактическая информация о плагине вынесена в **строковый пул `.rdata`**.

---

## 7. Блок идентичности в `.rdata`

Строковый пул после данных и GL-реестра содержит блок полей метаданных в
фиксированном порядке:

```
com.FXHOME.HitFilm.<Идент>              // ASCII, через 0x00
FXhome                                   // UTF-16, вендор
<Категория>                              // UTF-16, категория
<Отображаемое имя>                       // UTF-16, имя (переводится по Effect_<Идент>)
… литералы модуля: имена параметров, описания, копирайт …
```

Пример `Fade.hfpl` (аудио-переход, блок на смещении файла `0x1A00`):

```
com.FXHOME.HitFilm.AudioFade
46 00 58 00 68 00 6F 00 6D 00 65 00 00 00          => "FXhome"
54 00 72 00 61 00 6E 00 73 00 69 00 74 00 69 00 6F 00 6E 00 73 00 20 00 2D 00 20 00 41 00 75 00 64 00 69 00 6F 00 00 00  => "Transitions - Audio"
46 00 61 00 64 00 65 00 00 00                        => "Fade"
```

Сканер по всем 321 файлу (`hfpl_scan.ps1`): вендор `FXhome` и категория
прочитаны **у всех файлов**, пропусков нет.

### 7a. Проверка вызовом `PluginInfo` (5 сентября 2026)

Статическое чтение `.rdata` подтвердилось, но оно не единственный и не лучший источник: у модуля
есть штатный способ рассказать о себе, и он работает.

- **321/321** отдали метаданные. Первый прогон дал 317: `MotionTrack`, `AutomaticStabilizer`,
  `MotionLock` (OpenCV) и `LightFlaresV2` требуют модулей из **папки приложения**, уровнем выше
  `Plugins/`; с ней в пути поиска — 321/321.
- **Вендор `FXhome` у всех 321**, magic `0x089E31C7` у всех, версия пакета **5.0** у всех.
- **У каждого плагина есть свой GUID** (поле `+0x04` блока метаданных) — 321 уникальное значение.
  Это тот самый `fxtl::FXID`, по которому ищет `PluginByID`, и он решает проблему дубля
  идентификатора: `DepthMask.hfpl` и `DepthMatte.hfpl` объявляют один и тот же
  `com.FXHOME.HitFilm.DepthToMatte`, но GUID у них разные.
- **Идентификатор расходится с именем файла у 59 из 321** — не только у `360LightsaberV2Auto`.
  Примеры: `BoxBlur → Blur`, `Flame → Fire`, `FlyEye → InsectVision`, `FreezeFrame → Stutter`,
  `FrameBlendedRetiming → Speed`, все `Lightsaber* → Lightsword*`.
- **Имя несёт ключевые слова поиска в фигурных скобках** — у 138 из 321:
  `"Chroma Key {greenscreen green screen bluescreen}"`. Отображаемое имя — часть до `{`.
- **Категория может нести подкатегорию после `|`** — у 21 из 321: `"Keying|Matte Enhancement"`,
  `"Transitions - Video|Dissolve|Motion|Wipe|Zoom"`. Верхних категорий 26 — ровно те, что в таблице
  выше; полных строк категорий 31.
- **Тип модуля совпал с папкой поставки у всех 321**, что даёт точный ремап типа
  (`0 → 2D, 1 → VideoTransitions, 2 → AudioTransitions, 3 → Audio, 4 → Geometry, 5 → Behavior`) —
  в `RE_Plugin_Engine.md` он был записан с тремя перестановками.

Стоимость: **~58 мс на модуль**, ~18.6 с на все 321 — почти целиком `DllMain` самого плагина.

**Идентификаторы по всей поставке — 26 категорий:**

| Категория | Кол-во | | Категория | Кол-во |
|-----------|-------:|-|-----------|-------:|
| Behavior | 43 | | Particles & Simulation | 6 |
| Generate | 40 | | Temporal | 5 |
| Color Correction | 22 | | Animation | 5 |
| Color Grading | 20 | | Geometry | 4 |
| 360° Video | 18 | | Scopes | 4 |
| Keying | 17 | | Gradients & Fills | 4 |
| Audio | 16 | | Scene | 3 |
| Distort | 16 | | Sharpen | 3 |
| Transitions - Video | 15 | | Depth | 2 |
| Grunge | 15 | | Transitions - Audio | 2 |
| Stylize | 14 | | Lights & Flares | 13 |
| Warp | 11 | | Video Clean-up | 9 |
| Blurs | 8 | | Channel | 6 |

Идентификатор **не обязан совпадать с именем файла** и даже может коллизировать:

- `360LightsaberV2Auto.hfpl` → `com.FXHOME.HitFilm.360LightswordAuto`
- `DepthMask.hfpl` и `DepthMatte.hfpl` → оба `com.FXHOME.HitFilm.DepthToMatte`
  (при таком дубле порт подставляет `native:`-фолбэк по имени файла)

---

## 8. Встроенные шейдеры: GLSL 4.10, скомпилированный в рантайме

GPU-плагины несут **исходники GLSL** как ASCII-строки в `.rdata`. По всей
поставке найдено **3585 вхождений** `#version`, и все без исключения — 
`#version 410` (десктопный core, никакого GLSL ES / `precision highp`).

Первый (вершинный) шейдер одинаков во всех GL-плагинах — это инвариант
полноэкранного квада с двумя матрицами:

```glsl
#version 410
in vec4  v62af691094f5eb98494049af7f4b980f;
in vec2  v5ebd5fa185f5a28ef90669230aff3ca7;
uniform mat4 vbaef96411e8ba5638375b23c8abbbc15;
uniform mat4 va96ae5182131c77c37e85aa3cbfd6f1b;
out vec2  v1228a39b0f5c5a73bf598185f44aac06;
void main(){
  v1228a39b0f5c5a73bf598185f44aac06 = v5ebd5fa185f5a28ef90669230aff3ca7;
  gl_Position = vbaef96411e8ba5638375b23c8abbbc15 * va96ae5182131c77c37e85aa3cbfd6f1b * v62af691094f5eb98494049af7f4b980f;
}
```

Фрагментный — источник сэмплирования текстуры (пример `Invert.hfpl`):

```glsl
#version 410
uniform sampler2D v6bfe7db55508b0d55ecc18f3d2303ad4;
in vec2 v1228a39b0f5c5a73bf598185f44aac06;
void main(){ v25dc0a8bcd0d495c4b4242b9ad198469 = texture(v6bfe7db55508b0d55ecc18f3d2303ad4, v1228a39b0f5c5a73bf598185f44aac06); }
```

- Имена переменных **обфусцированы детерминированно**: 32-hex идентификаторы
  (похожи на хеши исходных имён), и для одного и того же исходника одинаковы во
  **всех** плагинах сборочного комплекта (вершинный инвариант `Invert` ≡
  `ChromaKey`). Это build-time переименование, а не рандом на загрузку.
- Шейдеры компилируются в рантайме через GL-функции, поэтому в плагин вшит
  полный **реестр имён расширений** из gl.xml (списки `GL_*`, `GL_*_ARB`,
  `GL_NV_*`, …) — отсюда основной объём `.rdata` у GL-плагинов и строки ошибок
  типа `ShaderCompile`/`ShaderLink` (сообщения шейдерного компилятора).
- Аудио-плагины (`Balance`, `Fade`, …) не содержат ни `#version`, ни GL-кода —
  чистая CPU-обработка.

---

## 9. Загрузка хостом (резюме по ссылкам)

- Менеджер плагинов — единственный `Tannen.dll`, поиск по маскам `*.hfpl` /
  `*.hfplx`; `biff::tannen::PluginFile::ReadPluginMetadata` (`0x18035adb0`)
  вызывает `PluginInfo` и заполняет структуру-метаданные (буферы имени 1024 Б,
  категории 600 Б, вендора 200 Б, копирайта 800 Б, идентификатора 100 Б).
- Ключ: `Effect_` + хвост после `com.FXHOME.HitFilm.` — ровно 321 контекст
  `Effect_*` в `Translations/ja/VegasEffects_ja.qm` из §5.1 дистрибутив-отчёта.
- Подробности протокола `PluginInfo`/`Notify` и BiffHost — в §5.1
  Distribution-отчёта и в разделе загрузчика RE_VegasEffects.md.

---

## 10. Порт: два пути чтения

Парсер `hfpl_scan.ps1` (PS7, чистый PE-разбор, `LoadLibrary` не вызывается)
прогнал **все 321 файл** и подтвердил 1:1 счёт прежних отчётов:

- машина/тип/подсистема/6 секций/DllCharacteristics — у всех совпадают;
- ровно 2 экспорта (`Notify`, `PluginInfo`), `Reloc` есть у всех, секций норм;
- TimeDateStamp единый (2023-08-01);
- идентификатор, вендор `FXhome`, категория прочитаны у **321/321**.

Результаты сохранены в `hfpl_inventory.csv` (321 строка, поля: путь, размер,
машина, секции, экспорты, идентификатор, shortId, вендор, категория).

### 10a. И проверка чтения **с** загрузкой (5 сентября 2026)

Порт теперь делает обе ступени, в том же порядке, что и референс:

1. **Статический гейт** — разбор таблицы экспорта; нет `PluginInfo` или `Notify` — файл отброшен,
   ничего не запускалось. Это `"Missing API"` оригинала.
2. **Вызов `PluginInfo`** у прошедшего гейт файла — под SEH, с буферами по контракту §4.3
   `RE_Plugin_Engine.md`, с `FreeLibrary` сразу после (референс делает то же самое).

Валидация — референсная: magic, `major < 22` (и `21 → minor < 2`), тип 0..5.

Повторный скан читает JSON-кэш (ключ — путь, размер и время изменения), поэтому 18.6 с
превращаются в 14 мс. `OPENVEGAS_NO_NATIVE_PLUGIN_LOAD=1` возвращает поведение «только статика».

### 10b. Исполняемый жизненный цикл и GPU-зонд (13 сентября 2026)

`NativePluginRuntime` теперь держит постоянные блоки `BiffHost` (336 байт) и `tagBiffAPI`
(1296 байт), вызывает `Notify(0)`/`Notify(1)` под SEH и реализует bootstrap-слоты:
`+0x40` (имя хоста), `+0x48` (BIFF HostEdition `0x9c4` для VEGAS) и `+0x1f8`
(magic/версия пакета). Число 6000 относится к product edition HitFilm и не является значением
этого callback; это исправлено, а текущий кэш метаданных с результатом `Notify(2)` имеет версию 5.

`hfpl_runtime_probe` создаёт offscreen OpenGL 4.1 Core context. На нём `Invert.hfpl` успешно
компилирует встроенные GLSL через `Notify(8)` и проходит frame path через `Notify(10)`:
`10,20,30,255` превращается в `245,235,225,255`. Трассировкой подтверждены GL-resource callbacks
`+0x4a8/+0x4b0/+0x4b8`.

Эти callbacks, frame/input-texture blocks, FBO и readback перенесены в
`src/plugin/NativeEffectRender.cpp`; `EffectRender` вызывает его из обычного пути таймлайна.
Нетривиальный TLS-объект заменён ленивым runtime: иначе `QOpenGLContext` создавался до `main`.
В рабочем `RenderWorker` сама `QOffscreenSurface` создаётся через GUI-thread, а context, DLL и GL
resources принадлежат render-thread и освобождаются до его остановки.

`Notify(2)` у `Threshold` восстановил пары регистрации/чтения параметров:

| Регистрация | Чтение в `Notify(10)` | Тип |
|---:|---:|---|
| `+0x60` | `+0xb8` | scalar `float` |
| `+0x68` | `+0xc8` | целое число |
| `+0x70` | `+0xc0` | `bool` |
| `+0x78` | `+0xc8` | `enum`/`int` |
| `+0x80` | `+0xd0` | Point2D, два выходных `float*` |
| `+0x88` | `+0xd8` | угол (`float`, degrees/radians) |
| `+0x90` | `+0xe0` | RGB, три выходных `float*` |
| `+0x220` | `+0xb8` | `float` с отдельным property/display range |
| `+0x228` | `+0xc8` | `int` с отдельным property/display range |
| `+0x230` | `+0xc0` | action Button, импульсное значение `bool` |
| `+0x238` | — | динамический UTF-16 Label |
| `+0x240` | `+0x268/+0x270` | однострочный UTF-16 текст |
| `+0x248` | `+0x268/+0x270` | многострочный UTF-16 текст |
| `+0x250/+0x258/+0x260` | `+0x268/+0x270` | save/open file и directory path |
| `+0x338` | `+0x340` | Orientation, три выходных `float*` |
| `+0x370` | `+0x378` | Point3D, три выходных `float*` |

Для action-кнопок также восстановлен setter `SetBoolValue +0x1a0` с ABI
`(host, key, frame, bool, mode)`: обновлённое импульсное значение доступно последующим
чтениям свойства внутри того же `Notify`/render-вызова.
Диагностические вызовы `FisheyeWarp2`, `Sphere`, `DepthMatte`, `GoProLensReframe` и `MotionBlur`
локализовали обязательные сервисы: `GetLayerInfoV2 +0x128`, `GetActiveCamera +0x138`,
`GetCameraInfo +0x140`, `GetMotionBlurInfo +0x148` и `GetActiveLights +0x158`.
Для изолированного 2D-кадра они возвращают штатные пустые layer/camera/light результаты.

Значения ищутся по переданному модулем ASCII-ключу (`threshold`, `color1`, `source`, …), поэтому
анимированные значения, уже вычисленные `RenderManager` для текущего кадра, доходят до DLL без
отдельного формата сериализации. Пиксельно проверены GUI- и worker-thread пути всех 241 модулей:
`360Blur`, `360ChannelBlur`, `360FisheyeConverter`, `360Glow`, `360GlowDarks`, `Invert`, `InvertAlpha`, `AlphaBrightnessContrast`, `BezierWarp`, `BrightnessContrast`, `Bulge`, `ChannelMixer`,
`ChannelSwapper`, `ChromaticAberration`, `ColorBalance`, `ColorConverter`, `ColorCorrection`, `ColorDifferenceKey`, `ColorTemperature`, `Crop`, `CrushBlacksWhites`,
`Demult`, `DuoTone`, `Exposure`, `ExposurePro`, `Fill`, `FindEdges`, `FisheyeWarp`, `Gamma`, `HueShift`, `PolarWarp`, `Posterize`,
`Threshold`, `ThreeStripColor`, `Tint`, `TwoStripColor`, `Vibrance`, `Vignette`, `VignetteExposure`, `WhiteBalance`, `Levels`, `ChromaKey`, `Derez`. Например, `Threshold` на одном runtime переключает
результат `255,0,0,255 → 0,0,255,255` при пороге `5 → 95`, а `Gamma=2` даёт
`10,20,30 → 50,71,87`; автоматически извлечённые контролы также проверены на `Exposure=1`
(`10,20,30 → 20,40,60`) и `AlphaBrightnessContrast/Brightness=-100`
(`10,20,30,255 → 5,10,15,127`); `ColorCorrection` даёт `0,20,40,255` на проверочном
наборе, `HueShift`, `Posterize` и `Vibrance` дают соответственно `0,20,40,255`,
`0,0,11,255` и `15,28,41,255`, `ThreeStripColor` — `10,19,30,255`, а
`TwoStripColor` — `30,23,21,255`; `ColorDifferenceKey` даёт `9,18,27,226`, а `DuoTone` —
`18,0,47,255`; нейтральные `Levels`/`Derez` сохраняют `10,20,30,255`, а изменённый
`Derez` выдаёт прозрачный пиксель. Для пяти warp-модулей зонд использует контрастный кадр 32×32:
все 1024 пикселя меняются одинаково в основном и worker-thread (`360FisheyeConverter`,
`BezierWarp`, `Bulge`, `FisheyeWarp`, `PolarWarp`). `360Glow` меняет 988/1024 пикселей,
`Vignette` при рамке 16×16 — 902/1024, `VignetteExposure` — 979/1024;
`360Blur` и `360ChannelBlur` меняют 1024/1024. Результаты обоих потоков совпадают.
Дополнительно тем же двухпоточным зондом включены `AutoColor`, `AutoContrast`, `AutoLevels`,
`BleachBypass`, `CineStyle`, `ClassicCineStyle`, `ColorVibrance`, `CrushBlacksWhitesAlpha`,
`CustomGray`, `DayForNight`, `Emboss`, `HSL`, `HueColorize`, `HueKey`, `NeonGlow`,
`RemoveStockBackground`, `ShadowHighlight`, `SpillSuppressor` и `YUVColorCorrection`.
Контрастный тестовый кадр дополнительно подтвердил пространственные эффекты `AngleBlur`,
`BilateralBlur`, `BoxBlur`, `ChromaBlur`, `Diffuse`, `EdgeDistortion`, `FlyEye`,
`HighpassSharpen`, `LensBlur`, `Magnify`, `Mosaic`, `RadialBlur`, `Sharpen`, `Twirl`,
`Unsharpen`, `WarpVortex`, `Waves` и `ZoomBlur`: они меняют от 632 до 1024 пикселей из 1024.
После подключения пустых layer/camera/light результатов `FisheyeWarp2` меняет 1023/1024,
а `Sphere` — 1024/1024 пикселя в обоих потоках.
Тем же путём подтверждены `PageCurl`, `PerspectiveWarp`, `PiP`, `PondRipple`, `Projector`,
`QuadWarp`, `Reflection` и `RollingShutter`; они меняют от 240 до 1024 пикселей.
После удаления неявного combined depth/stencil из выходного FBO `Tiles` меняет 1024/1024,
а `Wireframe` со своей scratch depth-текстурой — 256/1024 пикселей; результаты потоков совпадают.
Тем же изолированным двухпоточным прогоном подтверждены `4PointRamp`, `Cartoon`, `ColorPhase`,
`DotMatrix`, `DropShadow`, `FilmGrain`, `GlowDarks`, `Grain`, `HalfTone`, `Letterbox`, `Noise`,
`PencilSketch`, `RadialGradient`, `Ramp` и `YUVColorTransform`.
Контрастный 32×32 прогон дополнительно подтвердил `Dehaze`, `Deinterlace`, `Glow`, `LeaveColor`,
`LuminanceKey`, `OilPainting`, `ScanLines`, `Solarize` и `ToneColoring`: они меняют от 511 до
1024 пикселей одинаково в обоих потоках.
Следующий изолированный прогон включил ещё 62 пространственных, генеративных, световых и
стилизующих модуля. В их числе `DepthMatte`, `GoProLensReframe`, `DisplacementMap`,
`FluidDistortion`, `LensDistort`, `PixelSort`, `SurfaceRayTrace`, `GrainRemoval`, `LightFlares`,
`ProSkinRetouch`, `RainOnGlass`, `TVDamage`, 360-варианты и процедурные генераторы.
Повторная изолированная матрица после завершения scratch texture/VBO/renderbuffer services
включила ещё 42 модуля. Среди них `AudioSpectrum`, `AudioWaveform`, `Denoise`, `Echo`,
`ForceMotionBlur`, `FrameBlendedRetiming`, `FreezeFrame`, `MotionBlur`, `TimeDisplace`,
`TimeWarp`, `AnamorphicLensFlare`, `AutoVolumetrics`, `Lightning`, `Portal`, `Shake`,
`DepthMask`, `DifferenceKey`, `DistanceField`, `EnvironmentMapTransform` и
`EnvironmentMapViewer`. Каждый прошёл `Notify(8/10)`, readback и дал одинаковый результат
в GUI- и worker-thread.

Следующий прогон восстановил `tagBiffAPI+0x28`, `MaskIndex +0x4e8`, пустые коллекции
масок/контуров текста, `GetNumberOfKeyframes +0x1e8`, `SetStringValue +0x278` и
`HostOptions +0x350`. Исправлено извлечение path editor: `Cube LUT (*.cube)` хранится как
фильтр диалога, а не путь по умолчанию; multiline capture больше не обрезает XML preset
LightFlaresV2 до 511 символов.

Финальный прогон включил оставшиеся 10 2D-модулей: `360Text`, `AutomaticStabilizer`,
`CreditsTextCrawl`, `Histogram`, `MotionLock`, `Puppet`, `Text`, `Vectorscope`, `Waveform`
и `WaveformParade`. Их общий недостающий контракт состоял из source UUID по
`RenderContext+0x08`, настоящего module-specific instance state через `Notify(3/4)` и
успешного подтверждения `RedrawCustomUI +0x2c8`. Для старых эффектов, которые не реализуют
`Notify(3)`, сохранён постоянный минимальный instance block. Полная матрица дала 241/241
успешных рендера как в основном, так и в отдельном render-thread.

Для `VideoTransitions` общий 2D frame block оказался неверным: у перехода второй источник и
output target занимают отдельные слоты. По `Tannen::PluginVideoTransition::Render` восстановлена
раскладка `+0x10/+0x18` (два входа), `+0x20` (выход), `+0x38/+0x40` (матрицы),
`+0x48/+0x4c` (размер) и `+0x54/+0x58/+0x60` (sample/start/end). Новый двухвходовый зонд
подтвердил 15/15 переходов в основном и отдельном render-thread; ранее падавшие `ClockWipe`,
`LightLeakTransition` и `RadialWipe` теперь завершают `Notify(10)` и readback.

Для `Audio` восстановлен отдельный CPU-контекст `PluginAudioEffect::Render`: число каналов
находится в `+0x10`, sample rate в `+0x14`, interleaved PCM16-буфер в `+0x20`, число sample
frames в `+0x2c`, позиции чтения параметров в `+0x34/+0x38`, позиция блока в `+0x40`.
Эффект изменяет переданный буфер на месте. Постоянный runtime вызывает `Notify(8)` и `Notify(3)`
один раз, сохраняет module-specific state между блоками и освобождает его через `Notify(4)`.
Для эффектов с историей подключены CPU-варианты `GetLayerInfoV2 +0x128` и
`GetAudioSamples +0x160`: последний очищает переданный размер в байтах и возвращает mono
float samples с фактическим количеством, как исходный Tannen callback. `LayerID +0xe8` возвращает штатный null FXID
`00000000-0000-0000-0000-000000000000`. Это отдельно требуется `DopplerShift`: пустая C-строка
ошибочно воспринимается модулем как существующий слой.

У `AudioTransitions` контекст другой: число каналов `+0x0c`, input A `+0x18/+0x20`,
input B `+0x28/+0x30`, output `+0x38/+0x40`, текущая позиция/длительность/общее число samples
`+0x44/+0x48/+0x4c`. `CrossFade` по этим полям вычисляет отдельный вес каждого sample frame.
Режимы probe `audio` и `audio-transition` подтвердили 16/16 и 2/2 модулей без диагностических
заглушек, с одинаковыми PCM checksums в основном и отдельном worker-thread. Все 18 модулей
зарегистрированы как ABI-проверенные; 16 Audio включены в рабочий UI/export path, а два
AudioTransition остаются недоступны в UI до появления модели перекрытий. Для финального экспорта клипы с Audio `.hfpl` предварительно
декодируются FFmpeg в stereo PCM16/48 kHz с учётом source range и speed, последовательно
обрабатываются нативными модулями, записываются во временный raw PCM и входят в существующий
gain/delay/amix. Модель перекрытий AudioTransition и realtime audio preview пока отсутствуют.

Для `Behavior` восстановлены три отдельные точки ABI из `PluginFile`: `Notify(102)`
(`TransformationAtTime`, capability `6`), `Notify(104)` (`OpacityAtTime`, capability `7`) и
жизненный цикл instance через `Notify(3/4)`. В обоих frame-контекстах `+0x00` содержит layer ID,
`+0x08/+0x0c` — timeline/local frame; transform-контекст хранит в `+0x18` указатель на
column-major матрицу 4x4, opacity-контекст — inline `float`. Подключены host services
`GetPreBehaviorEffectTransformation +0x300`, `GetOriginTransformation +0x308`,
`GetLayerEularAngles +0x310`, `GetLayerOrientation +0x318`, `GetLayerScale +0x320`,
`GetLayerPosition +0x328` и `GetLayerAnchorPoint +0x330`. Runtime также подтверждает повторную
регистрацию float/layer-picker контролов при создании instance и `ClearPersistentMessage`.

Режим probe `behavior` выполняет каждый модуль с параметрами по умолчанию в основном и отдельном
worker-thread и сравнивает opacity и все 16 элементов матрицы. Основной ABI `102/104` подтвердил
19 модулей:
`DownInsert`, `DownRoll`, `Drop`, `Expansion`, `FadeBehavior`, `FlyInFadeOut`, `FlyInFlyOut`,
`FlyToZoomIn`, `LeftRoll`, `PositionMix`, `RightRoll`, `RotateByLayer`,
`StretchAndZoomIn`, `TinyZoom`, `TwirlBehavior`, `UpInsert`, `UpRoll`, `ZoomIn`, `ZoomOut`.
Они включены в UI и render/export path: эффекты изображения выполняются первыми, затем Behavior
матрицы последовательно применяются вокруг центра холста, а opacity перемножается с opacity слоя.

`Project.dll::CompositionAsset::SimulateLayers` восстановил отдельный `Notify(103)` путь и
покадровый кэш состояния слоя размером `0xb4`. `tagBiffBehaviorSC` содержит object index в
`+0x00`, время/шаг в секундах в `+0x18/+0x20`, opaque callback state в `+0x28`, указатель на
массив 0x80-байтных layer-state в `+0x30`, force callback в `+0x40` и drag callback в `+0x48`.
Первый имеет ABI `(state, index, x, y, z, SimulationForceMode)`: mode 0 суммирует прямое
ускорение, mode 1 — отдельный force-вектор с нормализацией. Второй перемножает коэффициенты
демпфирования. Хост за каждый шаг вычисляет `damping^dt`, затем
`velocity = damping * previousVelocity + acceleration * dt` и
`position += velocity * dt`.

Runtime повторяет этот порядок с фактическим FPS композиции. При 30 fps за одну секунду
`Acceleration` с 360 px/s² даёт 186 px, а `Gravity` с −500 px/s² — −258.333 px; probe проверяет
эти числа и совпадение GUI/worker-thread. В production несколько simulation-Behavior теперь
собираются в один список: на каждом шаге все модули добавляют силы и damping в общий accumulator,
после чего хост один раз обновляет общие velocity/position. Поэтому `Drag` влияет на скорость,
созданную `Acceleration` и `Gravity`, и также включён. Режим probe `behavior-stack` проверяет эту
комбинацию в основном и worker-thread: за секунду при значениях по умолчанию получается одинаковая
позиция `(171.958, -238.831, 0)`. Layer-state массив дополнительно включает
`AttractTo`, `Follow` и `RepelFrom`; `Throw` подтверждён с исходным Acceleration Time = 0
после восстановления импульса при `t=0`.
Рабочий итог составляет 27/43 Behavior. `MotionTrack` не входит
в whitelist: поставляемый модуль возвращает unsupported для `Notify(102/103/104/105)` и не создаёт
матрицу либо opacity. Текстовые модули требуют
`SubObjectTransformationAtTime` (`Notify(105)`). `OrientationValue +0x340` уже возвращает
сохранённый трёхкомпонентный параметр этим обработчикам.

`PluginBehaviorEffect::SubObjectTransformationAtTime` (`Tannen.dll` VA `0x18034c810`)
передаёт в `PluginFile` контекст MC размером `0x78`: ID слоя в `+0x00`, два frame index в
`+0x08/+0x0c`, исходную матрицу 4×4 в `+0x18`, число подобъектов в `+0x58`, указатель на
их записи в `+0x60`, две пары размеров в `+0x68..+0x74`. Каждая запись — `0x5c` байта:
матрица в `+0x00`, opacity float в `+0x40`, байт ClipValue в `+0x44` и пять float в
`+0x48..+0x58`. Capability `8`, message `105`. Native wrapper копирует эти записи туда и
обратно, так что модуль меняет каждый символ отдельно. Дизассемблирование `Typewriter.hfpl`
подтвердило чтение count/pointer и умножение opacity по адресу `record+0x40`.

`behavior-subobject` probe с длительностью 120 кадров и FPS 30 подтвердил вызов на 16
текстовых модулях. В частности, на кадре 15 `Typewriter` для восьми глифов выдаёт
`0.424264,0,0,0,0,0,0,0`; `DropInByChar` изменяет и матрицу, и opacity первого глифа.
Только `Typewriter` включён в production: `TextRender` сохраняет отдельный path каждого
сформированного Qt глифа и применяет возвращённую opacity к fill и stroke. Матрицы,
ClipValue и остальные 15 модулей остаются в probe до восстановления их геометрического
применения к глифам. `MotionTrack` требует отдельного контекста.

Штатный сканер теперь вызывает `Notify(2)` в том же загруженном экземпляре, в котором проверяет
`Notify(0/1)`, и перехватывает регистрации до уничтожения временных строк DLL. Пары `+0x50/+0x58`
восстанавливают вложенные группы, `+0x60/+0x68/+0x70/+0x78/+0x90` — float, int, bool, enum и RGB.
Для enum `+0x78` передаёт варианты одной UTF-16 строкой через `|`, поэтому панель получает настоящий
выпадающий список и его индекс по умолчанию. По карте `PluginFile::CreateAPI` из `Tannen.dll`
добавлены string/multiline/save-file/open-file/directory регистрации `+0x240..+0x260` и
runtime-чтение UTF-16 через `StringValueLength +0x268`/`StringValue +0x270`. Следующий проход
по той же функции восстановил Point2D/Angle/Orientation/Point3D registrations и value callbacks.
`CreateFloatSliderWithDisplayRange +0x220` и `CreateIntSliderWithDisplayRange +0x228`
декодируются отдельными ABI-точными callback: property-range берётся из аргументов 5/6,
display-range из 7/8, default из аргумента 9. `CreateButton +0x230` и `CreateLabel +0x238`
добавляют 18 импульсных action-кнопок и 7 нередактируемых labels. Полный скан теперь извлекает
5403 контрола.
Поставляемые 2D-модули не регистрируют Point3D/Orientation, однако их API-слоты подключены.
Результат кэшируется вместе с метаданными (cache v10).

Массовый прогон всех 241 2D-модулей извлёк **5403 контрола у 241/241 модулей**: 3377 float,
334 Angle, 221 Point2D, 322 bool, 334 RGB, 510 enum, 171 целый, 18 Button, 7 Label,
44 single-line string,
64 multiline string и один open-file.
`Notify(2)` завершился у **241/241**. Для `ChromaKey`, `ColorCorrection`, `Derez` и `Levels`
восстановлен `tagBiffAPI[0]`: это указатель на вложенную legacy-таблицу, из которой модули
вызывают сервисы `+0x08`/`+0x18`. Ранее нулевой указатель обрывал регистрацию параметров;
теперь каждый модуль завершает регистрацию штатно.

Следующий проход по `Tannen.dll::PluginFile::Render` восстановил собственно frame ABI.
Хост кладёт `tagBiffRenderContext*` в `tagBiffAPI+0x20`; точная функция
`Plugin2DEffect::Render` (RVA `0x333890`) пишет входной/выходной `tagBiffTexture` в
`+0x10/+0x18`, коэффициент нормализации в `+0x24`, scale в `+0x2c/+0x30`, пять
transform-указателей в `+0x38..+0x58` и размеры кадра в `+0x60/+0x64`. В
`tagBiffTexture` подтверждены texture target `+0x04`, pixel format `+0x08` и scale
`+0x2c/+0x30`; рабочие targets используют float RGBA. В таблицу также добавлены подтверждённые `ComboBoxValue +0xb0`,
`AngleValue +0xd8` и `NotifyProgress +0x178`. После этого `ColorCorrection`, `Derez`,
`Levels` и `ChromaKey` возвращают успех из `Notify(10)`. Реализован renderer-owned scratch
texture pool: ресурсы резервируются, переиспользуются по точному формату и удаляются вместе с
контекстом; transfer state изолирован от оставленного плагином unpack PBO. Восстановлены также
scratch VBO (`+0x4b0/+0x4b8`) и renderbuffer (`+0x500/+0x508`) pools. Нулевой `+0x24`
давал `NaN/Inf` в BoxBlur uniforms и fast-fail NVIDIA; после значения `1.0` `ChromaKey`
штатно завершает шесть scratch-проходов, `Notify(10)`, синхронизацию и readback без GL errors.
По декомпиляту `Tannen::PluginFile::CreateAPI` подключены texture-source callbacks:
`GetLayerTexture +0x100`, `GetSourceTexture +0x118`, `GetLayerTextureV2 +0x130` и
`GetLayerDepthTexture +0x2f8`. Первые три копируют 0x34-байтный `tagBiffTexture` входа из
`RenderContext+0x10`; depth callback очищает результат и возвращает референсный `-4`, поскольку
у обычного 2D source нет отдельной depth plane.
Модуль включён в production whitelist.

---

## 11. Выводы

1. **Формат `.hfpl` — это обычный PE-library x64** с двумя экспортами
   (`PluginInfo`, `Notify`), и вся метаинформация вынесена в строковый пул
   `.rdata`: ASCII-идентификатор `com.FXHOME.HitFilm.<Id>` + UTF-16 вендор,
   категория и имя. Ресурсов, кроме манифеста `asInvoker`, нет.
   **Уточнение:** строковый пул — это то, что видно снаружи; штатный способ получить эти поля —
   вызвать `PluginInfo`, который вдобавок отдаёт тип, GUID, magic, версию и четыре флага (§7a).
2. **Весь комплект собран в одну дату** (2023-08-01) и подписан одной
   Authenticode-подписью FXHOME (блоб 11800 Б одинаков у всех).
3. **Рендер на GPU (OpenGL): 265 из 321.** Все 2D-эффекты (241) и все
   видео-переходы — GL; аудио — CPU; в `Behavior`/`Geometry` смесь.
4. **Шейдеры компилируются из исходников в рантайме**: GLSL 4.10 вкомпилирован
   текстом с детерминированной обфускацией имён; большой объём `.rdata` — это
   реестр GL-расширений и строк компилятора.
5. Трекинг на базе **OpenCV 4.6** (`opencv_world460`), текстовые эффекты зовут
   системные API (GDI/оболочка).

---

## Источник данных

- 321 файл: `SAMPLES/VEGAS_Effects/Plugins/` (2D 241, Behavior 43, Audio 16,
  VideoTransitions 15, Geometry 4, AudioTransitions 2).
- Сканер: `C:\Users\Admin\AppData\Local\Temp\opencode\vegre\hfpl_scan.ps1`.
- Результат скана: `C:\Users\Admin\AppData\Local\Temp\opencode\vegre\hfpl_inventory.csv`.
- Зонд `PluginInfo` (5 сентября 2026): `hfpl_probe.cpp` (один модуль, дамп блока) и
  `hfpl_all.cpp` (все 321, TSV). Результат — `RE_HFPL_Plugins/hfpl_metadata.tsv` в этом репозитории.
- Порт-тест: `plugin_test.cpp` — 48 проверок против настоящих 321 модуля.
- Выборки: `Balance.hfpl`, `Invert.hfpl`, `ChromaKey.hfpl`, `MotionTrack.hfpl`,
  `Extrude.hfpl`, `CrossDissolve.hfpl`, `Text.hfpl`, `Fade.hfpl`.
- Метод: прямой PE-разбор, таблицы импорта/экспорта, строки (ASCII/UTF-16), защищённые вызовы
  `PluginInfo`/`Notify(0/1/2/8/10)`, трассировка ABI и пиксельные OpenGL-пробы.
