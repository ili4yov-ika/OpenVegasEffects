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

**Время 2D-эффектов (3 октября 2026).** `Plugin2DEffect::Render` (Tannen `0x180333890`) кладёт в
RenderContext (0xc8 байт) кроме текстур, матриц и размера ещё `+0x6c/+0x70` — время и время слоя
в мс, `+0x98/+0x9c` — кадр и кадр слоя, `+0xa0` — длину слоя в кадрах; кадр слоя за концом
(или до начала) держится на последнем (первом), а время и кадр шота сдвигаются на ту же величину.
Порт оставлял эти поля нулями, и всё, что движется само, стояло на моменте 0. Порт теперь
заполняет их (`plugin::NativeFrameTime`, из `RenderWorker::applyClipEffects`). Режим зонда
`render-time` (узор 64×64, кадры 0 и 30 слоя в 120 кадров) показывает **35/241** модулей,
меняющих картинку со временем: `360LightsaberV2Manual`, `AutomatedLightsword`, `CenterWipe`,
`Clouds`, `Cosmos`, `CreditsTextCrawl`, `EdgeDistortion`, `Electro`, `EnergyDistortion`,
`Evaporate`, `FilmDamage`, `Flame`, `FluidDistortion`, `HeatDistortion`, `LightLeak`,
`Lightning`, `LightsaberV2Manual`, `LightswordGlow`, `LinearWipe`, `ManualLightsword`,
`Pinwheel`, `PondRipple`, `Portal`, `PulpScifiTitleCrawl`, `RadialReveal`, `RadioWaves`,
`RainOnGlass`, `Shake`, `Shatter`, `SmokeDistortion`, `TimeDisplace`, `TimeWarp`, `TVDamage`,
`WaterCaustics`, `WitnessProtection`. Режим `render` по-прежнему 241/241. Ключ кэша кадров шотов
с нативными модулями получил метку `native-time-1`, чтобы старые неподвижные кадры не
показывались. Тест `nativeEffectsMoveWithTime` (OpenGL). Не сделано: время в контексте
видеопереходов: `PluginVideoTransition::Render` (Tannen `0x1803c9380`) кладёт в `+0x08/+0x0c`
два целых (`param_6/param_7`), но ни один из 15 модулей их не читает — зонд `transition` даёт
одинаковый результат с 0/0 и 5000/2500; позиция перехода — `+0x54..+0x60`. У 2D-эффектов `+0x08` —
по-прежнему нулевой UUID слоя.

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
зарегистрированы как ABI-проверенные и включены в рабочий UI/export path; AudioTransition
работают на краях клипов по той же модели окон, что видеопереходы (`composition::transitionWindows`). Для финального экспорта клипы с Audio `.hfpl` предварительно
декодируются FFmpeg в stereo PCM16/48 kHz с учётом source range и speed, последовательно
обрабатываются нативными модулями, записываются во временный raw PCM и входят в существующий
gain/delay/amix. Realtime preview теперь подаёт каждый 480-frame stereo PCM блок в те же
проверенные Audio `.hfpl` перед gain и master mix. Runtime-ключ отдельный для каждого
экземпляра эффекта, отказ оставляет сухой сигнал. Локальный регрессионный тест с
`Balance.hfpl` фиксирует изменение каналов в итоговом `pcmMixed`; физический аудиовыход
тестом не измеряется. На каждом 10-мс блоке realtime mixer вычисляет keyframe-значения
по кадру исходной композиции; тест с `Balance` проверяет смену стороны на keyframe.
**Переходы на таймлайне (1 октября 2026).** До этого 15 VideoTransition и 2 AudioTransition
проходили ABI-зонд, но таймлайн их не вызывал: переход добавлялся как обычный эффект клипа, и
`applyNativeVideoTransition` в production не использовался. В `Project.dll` переход — отдельный
`biff::project::Transition` (`Length`, `RelativeStartFrame`, `PluginID`, `From/ToObject`),
привязанный к объектам трека как `Incoming/OutgoingTransition`. В порте он хранится записью
`Effect` с `transitionEdge` (`In`/`Out`) и `transitionSeconds`, поэтому строки таймлайна,
инспектор, keyframes, Undo и сохранение работают без отдельной модели. `composition/Transition.h`
вычисляет окна: `In` между соседними клипами слоя центрируется на стыке и проигрывает оба клипа
в их handles, у перекрывающихся клипов окном служит перекрытие, без соседа это fade из пустоты;
`Out` — fade в пустоту в конце клипа. Рендер кадра проводит оба клипа через эффекты/Behavior/маски
и `applyNativeVideoTransition`; недоступный модуль или выключенные эффекты дают premultiplied
dissolve. Инспектор показывает край и длительность, таймлайн рисует окно поверх клипов.

`PluginAudioTransition::Render` (`0x180345940`) передаёт больше полей, чем использовалось:
`+0x08` сторона (2 — уходящий объект до точки монтажа, 1 — входящий после неё), `+0x0c` каналы,
`+0x18/+0x28/+0x38` A/B/выход с числами frames, `+0x44` позиция и `+0x48` длина в одной единице,
`+0x4c` длина в samples, `+0x50` точка монтажа. `CrossFade` смешивает `A·(1−w)+B·w` и сторону не
читает; `Fade` гасит A до точки монтажа и поднимает B после неё, а при нулевой стороне ничего не
пишет — поэтому прежний зонд видел «тождество». Порт делит блок на стыке и передаёт сторону 2/1.
Realtime-микшер продлевает клипы перехода в handles, держит их вклады отдельно и заменяет их сумму
результатом модуля в окне; экспорт (`render/AudioExport`) делает то же на PCM всего клипа.
Тесты: dissolve и сохранение перехода, запасной кроссфейд и провал `Fade.hfpl` в тишину на стыке
в realtime, экспорт PCM с переходом.

**Geometry ABI восстановлен (1 октября 2026).** `PluginGeometryEffect::ProcessGeometry`
(`0x1803c7280` → `PluginFile::ProcessGeometry` `0x180359030`) проверяет тип модуля 4, кладёт
`tagBiffGeometryMC*` в `api+0x20`, тип контекста `api+0x498 = 5` и отправляет `Notify(101)`.
Хост — `Flux.dll`: `FUN_1804d94e0` строит геометрию **текста** (`TextBoxMaterial`,
`FUN_1804d8ed0` → `FUN_1805278b0`) и по очереди отдаёт её включённым Geometry-эффектам;
перед модулями с флагом `0x20000` (vtable `+0x58`) многоугольники заранее заливаются.

| Структура | Поля |
|---|---|
| `tagBiffGeometryMC` (0x30) | `+0x00` shape, `+0x08` callback `void(mc, shape)` (Flux `LAB_1804da310`), `+0x10/+0x14` время (параметры модули читают по `+0x14`), `+0x18` приёмник результата, `+0x20..+0x28` три int |
| `tagBiffGeometryShape` (0x10) | `+0x00` массив пакетов, `+0x08` число |
| пакет (0x80) | `+0x00/+0x08` вершины/число, `+0x10/+0x18` треугольники/число, `+0x20/+0x28` многоугольники/число, `+0x2c` 4 float габарита, `+0x3c` матрица 4×4 (column-major) |
| вершина (0x20) | позиция xyz, нормаль xyz, UV |
| треугольник (0x14) | 3 индекса, `material`, `group` |
| многоугольник (0x18) | `int*` индексы, число, `material` (`+0x0c`), `group` (`+0x10`), флаги (`+0x14`): `1` лицевая, `2` обратная, `4` внутренние рёбра; Bevel добавляет `8` |

Модуль копирует пакеты к себе (`FUN_180001f10`), меняет и отдаёт новый shape через callback
(`FUN_180003420` → `tagBiffGeometryShape` + новый MC); после `Notify` его память освобождается,
поэтому хост копирует результат в callback. Многоугольник — один контур грани: дырки глифа —
отдельные контуры той же грани. Extrude для двусторонних контуров (`3`) делает копию с
**обратной** нормалью и флагом `1`, исходный оставляет с `2`, сдвигает их по нормали и строит
стенки по рёбрам с нормалью `n × ребро`; отсюда соглашение хоста — нормаль от зрителя, внешние
контуры по часовой при взгляде спереди (Y вверх), дырки — против. `FUN_1804d7ac0` заливает
контуры: группы «обратные» (`2`) и «только лицевые» (`1`), проекция на плоскость по нормали
первой вершины, совместная заливка с дырками, затем многоугольники удаляются.
`RotateGeometry` вращает каждый пакет вокруг центра вершин по x/z и середины `+0x34/+0x38` по y
(общая ось строки). `BendGeometry` дополнительно проверяет хост (`api+0x48` = 0x898/0x9c4, путь
`\Plugins\`) и читает `centerPos` через `api+0x378` (Position3D, как `+0x340`).

Порт: `plugin::applyNativeGeometryEffect` (`NativeEffectRender`), геометрия текста, заливка и
меш — `render/TextGeometry`; `RenderWorker::renderTextGeometry` рисует текстовый клип с
Geometry-эффектами освещённым мешем (3D-слой — матрицей слоя и камерой сцены, 2D — 2D-размещением
под камерой, показывающей плоскость z = 0 в размер кадра; 2× суперсэмплинг). Зонд
`hfpl_runtime_probe <Geometry> <deps> geometry`: квадрат 100×100 с дыркой 50×50 → Extrude
(20) — 80 вершин, 64 треугольника стенок, z ∈ [−20, 20]; Bevel — 80/32 с флагами `9/10`;
RotateGeometry (y = 90°) — x = 50, z ∈ [−50, 50]; BendGeometry (45°, длина 100) — x ≤ 94.9,
z ≥ −28.8; результаты совпадают в основном и отдельном потоке. Тест
`textGeometryFillsGlyphsAndRunsGeometryModules`: «O» — два контура с нужной ориентацией,
заливка покрывает кольцо без дырки (ошибка площади < 0.1 %), Extrude даёт z ∈ [−20, 20],
клип рендерится мешем.

Не восстановлено: точная тесселяция Flux (`FUN_1805278b0` — разбиение кривых, UV, материалы
лица/стенок/фаски через `material`), обводка и подложка текста в 3D, Behaviors по глифам
поверх геометрии (их матрицы в Flux идут в `+0x3c` пакетов).

**MotionTrack: что известно (1 октября 2026).** `MotionTrack.hfpl` помимо `Notify(3/4)` обрабатывает свойства `Notify(5..7)`, фоновую обработку
`Notify(18)`, `TransformationAtTime` `Notify(102)` и custom UI вьюера `Notify(1002..1014)`
(мышь, клавиатура, фокус, `RedrawCustomUI +0x2c8`); сервисы трекинга —
`SaveTrackingData +0x3b0`, `GetNumberOfFeatures +0x3b8`, `GetTrackingData +0x3c0`,
`CreatePointLayer +0x3a0`, `ApplyTransformToLayer +0x3a8`. Без хостинга custom UI и фонового
трекинга кадров модуль не выдаёт матрицу.

**Custom UI вьюера восстановлен (1 октября 2026).** Tannen `PluginFile::CustomUI*`
(`0x18035cd50`..`0x18035de00`): Setup `Notify(1001)`, Shutdown `1002`, Render `1003`
(после `InitializeShaders`), MouseEvent `1004/1005/1006` (move/press/release — `MouseEventType` 0/1/2),
KeyEvent `1007/1008/1009`, GainFocus `1010`, LoseFocus `1011`, HasContextMenu `1012`,
ContextMenu `1013`; тип контекста `api+0x498 = 4`. Контекст (0xa8 байт, `api+0x20`):
`+0x00` строка ID слоя, `+0x08` имя контрола, `+0x10/+0x14` размер области, `+0x18/+0x20`
масштаб (double), у Render ещё `+0x28..+0x38` три double, `+0x40..+0x50` пять int (MotionTrack
передаёт `+0x40` в `GetLayerPixelTransform +0x400`, читает параметры по `+0x44`, кадр
признаков — `+0x4c`), `+0x54` матрица 4×4, `+0x98` double, `+0xa0` bool, `+0xa1` linear color.
Событие мыши (`api+0x18`): `+0x00/+0x04` точка, `+0x08` double «кнопка нажата» (MotionTrack
сравнивает с 0.5), `+0x20..+0x2c` четыре int. Событие клавиши: `+0x10` код (keysym X11:
MotionTrack ждёт `0xffe3/0xffe4` Ctrl и `0xffe9/0xffea` Alt), `+0x18` текст. Сервисы:
`CreateCustomUIControl +0x2c0`, `RedrawCustomUI +0x2c8`, `SetCursor +0x388` (код в host+0x20,
Tannen переводит 1/2/3/4/6/7/8/9/20 в курсоры Qt), `RequestBackgroundProcessing +0x398`,
`GetLayerPixelTransform +0x400`.

Порт: `nativeCustomUi*` в `NativeEffectRender` (GL-контекст `ThreadRenderer` потока GUI,
модуль видит область размером с холст и единичную матрицу, отрисовка читается в оверлей),
`ui::NativeCustomUiOverlay` во вьюере (Setup/Shutdown при смене выделенного эффекта,
события инструмента «Выделение», клавиши, фокус, курсор, перерисовка по запросу модуля).
Зонд `hfpl_runtime_probe MotionTrack.hfpl deps customui`: Setup/Shutdown/мышь/Render
возвращают 1, Ctrl down/up вызывают `RedrawCustomUI` (подтверждает `+0x10`), сбоев нет;
BendGeometry проходит ту же последовательность. Тест `viewerCustomUiReachesNativeModule`
(с OpenGL; на offscreen пропускается).

**MotionTrack на хосте порта (2 октября 2026).** Полный цикл модуля выполняется на его
собственном экземпляре (runtime custom UI, по одному на эффект — `NativeCustomUiView::instanceKey`):

- Сервисы Tannen восстановлены по `PluginFile::CreateAPI` и телам `PluginHostAPI::*`:
  `SetPropertyState +0xa8` (байт 0 — доступность контрола), `SetComboBoxValue +0x190` /
  `SetIntValue +0x1a8`, `SaveTrackingData +0x3b0 (host, layer, frame, count, from*, to*, affine[6])`,
  `GetNumberOfFeatures +0x3b8`, `GetTrackingData +0x3c0`, `GetAssetInfo +0x3c8` (возврат
  `tagBiffAssetInfo` 0x20 байт через скрытый указатель: `+0x00` число кадров, `+0x08` частота,
  `+0x10` кадр начала footage в слое, `+0x18` тип: 0 видео, 3 композит, 5 нет),
  `GetAssetTexture +0x3d0 (host, ctx, layer, frame, tagBiffTexture*)` — кадр исходника как
  GL-текстура (модуль читает её `glGetTexImage(GL_RED)`), `TranslateString/TranslateStringN/
  StringArgI/StringArgF +0x3e0..+0x3f8` (правка UTF-16 буфера на месте), настоящая
  `GetLayerPixelTransform +0x400` (единичная матрица 4×4). `GetLayerInfoV2 +0x128` отдаёт
  `tagBiffLayerInfo`: ID, `+0x28` тип, `+0x2c` стартовый кадр, `+0x34` длина, `+0x41` видимость,
  `+0x6c/+0x70` размер, `+0x74` мировая матрица (`WorldTransformationAtTime`). Данные трекинга
  хранятся на ассете (как `AbstractAsset::SetTrackedFeatures`), в проект не пишутся.
- `Notify(7)` (NotifyPropertyChanged): `api+0x18` → `{char* ключ, +0x14 кадр, +0x18 причина}`;
  причина Tannen: правка пользователем 0 → 1 (перед ней `Notify(14)`), 1 → 2, 2 → 3, 3 → 4.
  Значения, которые модуль ставит сам, хост ему обратно не сообщает.
- `Notify(18)` (NotifyBackgroundProcess) и `Notify(7)` получают render context поведения (тип 0,
  0xc8 байт): `+0x08` ID слоя, `+0x24` PAR, `+0x60/+0x64` размер, `+0x6c/+0x70` время и время слоя
  в мс, `+0x98/+0x9c` кадр и кадр слоя, `+0xa0` длина слоя. `RequestBackgroundProcessing(ms)`
  просит следующий вызов через `ms`.
- `Notify(5)/(6)`: данные экземпляра — модуль отдаёт байты колбэку `SerializeInstanceBytes`
  в `api+0x110` (у MotionTrack: «RTOM», источник, покадровые аффинные преобразования, признаки
  по кадрам с флагами — бит 1 = не выбран, история выделений), читает из `api+0x30/+0x38`.
- `Notify(102)` (TransformationAtTime): время в контексте — **миллисекунды**, как и у
  `SubObjectTransformationAtTime` (с кадрами MotionTrack отдавал 1/33 движения).
- Custom UI: `MouseEventType` 0/1/2 → `Notify(1004/1005/1006)` = **перемещение/нажатие/отпускание**
  (раньше порт путал нажатие и перемещение, и лассо MotionTrack каждый раз начиналось заново);
  матрица `+0x54` в `Notify(1003)` переводит пиксели слоя в clip space (с единичной модули не
  рисовали ничего — теперь BendGeometry рисует ось изгиба, MotionTrack — признаки).
- Ориентация установлена опытом с неподвижной верхней половиной кадра: пиксели слоя у модуля идут
  сверху вниз (кадр загружается верхней строкой вперёд, координаты мыши — пиксели области сверху
  вниз); результат `Notify(102)` хост сопрягает отражением по Y в пространство слоя порта (Y вверх).

Сценарий: «Motion From» → `Notify(7)` → «Pending» → `Notify(18)` «Footage Analysis : x%» (KLT в
модуле, `SaveTrackingData` по кадрам) → «Draw around the area you wish to track» → лассо в
вьюере → «Selecting Features» (асинхронная задача модуля переносит выделение по кадрам) →
«Calculating Transform : x%» → матрица. Зонд `hfpl_runtime_probe MotionTrack.hfpl deps track`
проходит его на синтетическом видео (текстура сдвигается на 2 px вправо и 1 px вниз за кадр):
29 пар кадров, 7453 точки, средний шаг (2.00, 1.00), к кадру 29 слой смещён на (58, −29);
с `OPENVEGAS_PROBE_STILL_TOP`/`OPENVEGAS_PROBE_LASSO_TOP` (верх неподвижен, лассо сверху) — ≈0.
Зонд с `OPENVEGAS_PROBE_NO_SETUP` проходит тот же сценарий без `Notify(1001)`: свойства и фоновая
обработка не требуют поднятого custom UI.

**MotionTrack в приложении (3 октября 2026).** `ui::NativeInstanceHost` делает то, что в эталоне
делают `PluginFile` и проект:

- Источник (`setNativeSourceHost`): `GetLayerInfoV2` — клип слоя (свой клип экземпляра или клип
  слоя footage, сильнее всего перекрывающий его), старт и длина в кадрах шота, размер содержимого;
  `GetAssetInfo` — видео (кадры по частоте шота, по времени в исходнике) или секвенция (свои кадры
  и частота), `+0x10` — кадр исходника, с которого начинается клип; `GetAssetTexture` — кадр из
  общего кэша `MediaManager::videoFrame` или декодер `VideoDecoder` (оригинал, не прокси),
  декодированный кадр кладётся в тот же кэш. Ответы только на потоке GUI.
- Вид для custom UI: `area` — пиксели footage (`motionFromLayer`), `target` — холст вьюера в
  пикселях отображения (W·PAR × H, как `ViewerMapping::canvasSize`), матрица `+0x54` — размещение
  слоя footage на холсте так же, как его рисует `RenderWorker::renderClip`; события мыши
  переводятся обратно через её обратную.
- Правка параметра в инспекторе (`EffectInspector::effectParameterEdited`) → `Notify(7)`; кнопка
  (`resumeProcessing`) — только нажатие. `RequestBackgroundProcessing(ms)` ставит экземпляр в
  очередь `QTimer` (задержка ≤ 1 с), `Notify(18)` повторяется, пока модуль просит. Значения,
  которые модуль ставит сам, пишутся в эффект без шага отмены.
- Когда модуль больше не просит фоновой работы (или свойство изменено без неё), хост берёт
  `Notify(5)` в `Effect::instanceData` (`<InstanceBytes>` base64, как
  `EffectInstance::SerializeInstanceDataBytes`) и считает `Notify(102)` для каждого кадра клипа.
  Движение модуля — в пикселях footage, Y вниз; на холсте это `P⁻¹·R·P` (P — размещение footage),
  в рендер оно идёт в терминах слоя (пиксели отображения от центра, Y вверх) через
  `setNativeInstanceTransforms`; `RenderWorker::applyClipBehaviors` отражает всю матрицу в строки
  изображения (включая поворот — у остальных Behavior знак поворота ещё не проверен).
- `sync()` перед каждым кадром: если набор эффектов MotionTrack с данными или их данные
  изменились (открытие проекта, смена шота, undo, перестановка/удаление эффектов), экземпляры
  получают данные `Notify(6)` и матрицы считаются заново; идущий анализ не прерывается.

- `SetPropertyState +0xa8` (`PluginHostAPI::SetPropertyState`, Tannen `0x180381c70`) отдаёт
  `tagPropertyState` экземпляру (vtable `+0x40`) и обработчику панели (`+0x18`). MotionTrack ставит
  `analysisStatus` = 1 на время анализа и 0 в конце, а `positionX/positionY/rotation/scale/
  selectionMode` = 1. Порт хранит состояние по экземпляру (`setNativeControlStates`,
  `nativeControlShown`) и скрывает в инспекторе строку выключенного контрола
  (`EffectInspector::applyControlStates`); состояния, пришедшие из `Notify(6)`, тоже учитываются.

Не сделано: footage — вложенный шот, мировые матрицы слоёв, `CreatePointLayer/ApplyTransformToLayer`,
скорость клипа footage; смысл байта 0 `tagPropertyState` (видимость/доступность) не подтверждён.

**Исправление 1 октября 2026: `GetSampleRanges` и входной буфер.** Прежний критерий «16/16»
проверял только код возврата и совпадение потоков. Проверка RMS показала, что 10 модулей
(`AudioEcho`, `AudioReverse`, `Cathedral`, `Equaliser`, `LargeRoom`, `MediumRoom`, `SmallRoom`,
`NoiseReduction`, `ShortwaveRadio`, `Telephone`) выдавали тишину. Контекст восстановлен по
`Tannen::PluginAudioEffect::Render` (`0x180341f80`, vtable `+0x178`) и `GetSampleRanges`
(`0x180341e60`, `+0x170` → `PluginFile::GetSampleRanges` `0x18035a6f0`, `Notify(12)`):

| Смещение | Поле |
|---:|---|
| `+0x08` | строка FXID слоя-владельца (передаётся модулем в `GetLayerInfoV2`/`GetAudioSamplesV2`) |
| `+0x10/+0x14` | каналы / sample rate |
| `+0x20` | interleaved PCM16 буфер: вход — склеенные запрошенные диапазоны, выход — первые `+0x2c` frames |
| `+0x28` | число **входных** frames (раньше не заполнялось → модули читали 0 frames) |
| `+0x2c` | число выходных frames |
| `+0x30` | кадр для `GetLayerInfoV2` |
| `+0x34/+0x38` | миллисекунды позиций `+0x48`/`+0x40` (`AudioEcho` сравнивает их разность с delay) |
| `+0x40` | позиция начала выхода, **локальная для слоя** |
| `+0x48` | позиция первого входного frame в буфере |
| `+0x58` | нижняя граница, которой модули ограничивают запрос через `max(first, +0x58)` |

Протокол: хост вызывает `Notify(12)`, модуль сообщает нужные диапазоны через
`NotifySourceSamples +0x2b8` (`PluginHostAPI` `0x180383e50`: плоский массив `int64`, пары
`[first, end)`); модули без message 12 возвращают 2, и вход совпадает с выходным диапазоном.
Затем хост склеивает диапазоны в буфер и вызывает `Notify(10)`. `Equaliser` запрашивает
`[start − half, …)` для FFT-свёртки, реверберации и Telephone/Radio — историю длиной в
импульсную характеристику (`0x2000..0x8000` отсчётов при 48 кГц), `AudioReverse` — зеркальный
участок от конца слоя (длина берётся из `GetLayerInfoV2+0x34` в кадрах и FPS
`GetTimelineInfo+0x18`), `NoiseReduction` — окна `0x800/0x400`. `Equaliser::Render` не читает
`+0x48` и всегда считает началом буфера `start − half`, поэтому нижняя граница `+0x58` не
должна обрезать запрос у начала слоя: порт передаёт `INT64_MIN`, а отсчёты вне слоя читаются
тишиной. С границей 0 первые блоки EQ и Radio смещались, после исправления результат всех
модулей, кроме `Pitch` (±34 из-за гранулярного состояния), не зависит от размера блока.

`GetAudioSamplesV2 +0x2b0` (`0x180375f20`) имеет ABI `(host, layerId, int64 first, int frames,
void* dest, int destBytes)`, очищает `dest` и для собственного слоя возвращает interleaved
PCM16 до текущего эффекта по позиции `first + StartFrame слоя`. Его использует `AudioEcho`, когда
история не входит в буфер. Позиции во всех audio-вызовах локальны для слоя.

Порт: `AudioThreadRenderer::processEffect` выполняет оба сообщения и собирает буфер из сухого
источника. Экспорт декодирует клип целиком, обрабатывает цепочку от начала клипа (статические
параметры блоками по 4096 frames, анимированные — по 480 с пересчётом значения на каждый блок)
и затем обрезает участок экспорта. Realtime хранит для каждого экземпляра эффекта историю
сухого сигнала (до 2^20 frames), поэтому EQ, Echo, реверберации и Telephone точны и при
воспроизведении. Кроме того, декодер клипа читается на 0,25 с вперёд, и первый эффект цепочки
видит этот участок как будущее (окна NoiseReduction, короткий Reverse); дальше окна и для
последующих эффектов будущее при воспроизведении — тишина, экспорт точен. Режим probe `audio` теперь обрабатывает 0,5 с сигнала
блоками 480 и 4096, сравнивает потоки и размеры блоков, считает тишину на выходе ошибкой и
печатает время: худший realtime-случай — `NoiseReduction`, 306 мс на 500 мс звука (Debug).

Для `Behavior` восстановлены три отдельные точки ABI из `PluginFile`: `Notify(102)`
(`TransformationAtTime`, capability `6`), `Notify(104)` (`OpacityAtTime`, capability `7`) и
жизненный цикл instance через `Notify(3/4)`. В обоих контекстах `+0x00` содержит layer ID,
`+0x08/+0x0c` — время и время слоя в миллисекундах (см. исправление ниже); transform-контекст хранит в `+0x18` указатель на
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

**Исправление 3 октября 2026: время, opacity и контекст слоя.** Кривые режима probe
`behavior-curve` (слой 120 кадров при 30 fps) показали, что прежний «подтверждённый» путь давал
неверную анимацию, хотя потоки совпадали:

- Время в `tagBiffBehaviorMC/OMC` (`+0x08` — время, `+0x0c` — время слоя) — **миллисекунды**, как
  во всём Project.dll (`VisualObject::BehaviorEffectTransformation` передаёт одно и то же время в
  мс обоим полям) и в `Notify(105)`. С кадрами модули раскрытия шли в 1000/fps раз медленнее:
  FlyInFlyOut за 120 кадров вырастал с 0.5 до 0.57 вместо 1.0 к 30-му кадру (25 %).
- Opacity модуль пишет в сам контекст (`+0x18`, `PluginFile::OpacityAtTime` кладёт туда 1.0, если
  модуль отказался); порт читал свою копию, и opacity всех Behavior была 1. Теперь FadeBehavior —
  0 → 0.26 → 0.71 → 1 на кадрах 0/5/15/30, FlyInFadeOut гаснет к концу слоя.
- `PluginBehaviorEffect::TransformationAtTime` (Tannen `0x18034b6d0`) заполняет MC не только ID и
  временем: `+0x20..+0x2c` — MinX, MinY, MaxX, MaxY слоя в его единицах, `+0x30/+0x34` — ширина и
  высота композиции. Текст (тип слоя 6): paragraph — `TextBox::MinX..MaxY`, point — ±ширина
  композиции/2 по X и строки по Y (высота первой строки вверх, остальные вниз; в порте высоту
  строки заменяет размер шрифта). Слой с ассетом (0) — `0..ширина` × `0..высота` ассета, Grade (4) —
  кадр. Матрица `+0x18` перед вызовом единичная. Drop, Down/Up/Left/Right Roll и Insert двигают
  слой этими числами и с нулями не делали ничего.
- `GetPreBehaviorEffectTransformation +0x300` (`FUN_18036a7d0`) возвращает
  `AbstractLayer::WorldTransformationAtTime` слоя без его Behavior; модули берут из неё позицию.
  Результат Behavior — отдельная матрица поверх слоя (`BehaviorEffectTransformation` перемножает
  результаты модулей начиная с единичной).
- Пространство — композиция от центра кадра, Y вверх (Drop поднимает нижний край слоя к +H/2),
  поэтому рендер отражает всю матрицу в строки изображения, включая поворот: прежде знак
  поворота/сдвига не менялся, и Twirl крутил в обратную сторону.

Порт: `plugin::NativeBehaviorLayer` (мировая матрица, границы, размер композиции) заполняет
`RenderWorker` (`behaviorLayerAt`). `behavior-curve` для слоя 400×100 в точке (300, 100):
DownInsert на кадре 0 поднимает слой на 490 (нижний край на +540 = H/2) и к 30-му кадру ставит на
место; LeftRoll проходит от +860 до −1440 по X; ZoomIn масштабирует 0.5 → 1 вокруг позиции слоя
(сдвиг 150, 50 = p·(1 − s)); PositionMix и RotateByLayer без целевого слоя не меняют матрицу.
Тест `nativeBehaviorsKnowTimeAndLayer`.

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
Рабочий итог составляет 42/43 Behavior. `MotionTrack` не входит
в whitelist: поставляемый модуль возвращает unsupported для `Notify(102/103/104/105)` и не создаёт
матрицу либо opacity. Текстовые модули требуют
`SubObjectTransformationAtTime` (`Notify(105)`). `OrientationValue +0x340` уже возвращает
сохранённый трёхкомпонентный параметр этим обработчикам.

`PluginBehaviorEffect::SubObjectTransformationAtTime` (`Tannen.dll` VA `0x18034c810`)
передаёт в `PluginFile` контекст MC размером `0x78`: ID слоя в `+0x00`, два времени в миллисекундах в
`+0x08/+0x0c`, исходную матрицу 4×4 в `+0x18`, число подобъектов в `+0x58`, указатель на
их записи в `+0x60`, две пары размеров в `+0x68..+0x74`. Каждая запись — `0x5c` байта:
матрица в `+0x00`, opacity float в `+0x40`, флаг ClipValue в `+0x44`, четыре float
в `+0x48..+0x54` и ещё один флаг в `+0x58`. Capability `8`, message `105`.
Native wrapper копирует эти записи туда и
обратно, так что модуль меняет каждый символ отдельно. Дизассемблирование `Typewriter.hfpl`
подтвердило чтение count/pointer, умножение opacity по адресу `record+0x40` и перевод
времени `MC+0x0c` из миллисекунд умножением на `0.001`.

`behavior-subobject` probe с длительностью 120 кадров и FPS 30 подтвердил вызов на 16
текстовых модулях. На кадре 15 `Typewriter` для восьми глифов выдаёт
`1,1,1,1,0.866025,0,0,0` и полностью раскрывает строку к кадру 30;
`DropInByChar` сдвигает первый глиф по Y примерно на −57 px; `StringFade`
даёт opacity 0.5 и масштаб 1.293. Все три совпадают между основным и worker-потоком,
их ClipValue остаётся выключенным. Дополнительно проверены `CentralSpiral`,
`CinemaStyle`, `DoomoDesigns`, `Random`, `Random2`, `RichTick`, `ShuffleIn` и
`WavyStyle`: при кадрах 0, 1, 15, 30, 60 и 120 все восемь дают конечные opacity,
плоские 2D-матрицы, выключенный ClipValue и совпадение потоков. Эти 11 модулей
включены в production: `TextRender` хранит
отдельный path и baseline origin каждого сформированного Qt глифа, применяет матрицу
вокруг этого origin и opacity к fill и stroke. `Flux.dll::FUN_180524a50` умножает
базовую матрицу глифа на behavior-матрицу и отдельно передаёт ClipValue в шейдер;
пять направленных текстовых модулей (`DownDirInsert`, `LeftDirInsert`,
`RightDirInsert`, `UpDirInsert`, `Push`) также подключены. `Flux.dll`
`FUN_180513fd0` задаёт `ecLocalPos = in_Position`, до умножения на
`cursorMatrix`; fragment shader `FUN_180513930` проверяет интервал
`clippingValues.x <= ecLocalPos.x <= clippingValues.y` и
`clippingValues.w <= ecLocalPos.y <= clippingValues.z`. `TextRender` переводит
этот Y-up прямоугольник в локальные координаты Qt-глифа, трансформирует его
вместе с глифом и обрезает заливку и stroke. Probe проверяет конечные значения,
плоские матрицы и совпадение ClipValue между потоками на кадрах 0/1/15/30/60/120;
регрессия проверяет пустую и частичную область. Краевая антиалиасинг-маска Qt
может отличаться от производной `dFdx/dFdy` в исходном GPU-шейдере.
`MotionTrack` требует отдельного контекста.

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
