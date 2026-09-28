# RE: Движок нативных плагинов `.hfpl` в VEGAS Effects

Чтение, встраивание, работа и управление плагинами HitFilm-формата в программе.
Отчёт строится на декомпиляции `Tannen.dll` (полный декомпил-дамп
`decompile-src/tannen_decomp_full`, база `0x180000000`) и на декомпиляции
реального плагина `Plugins/2D/Invert.hfpl` (Ghidra 12.1.2, проект
`D:\ghidra_projects\HFPL`).

Источники и перекрёстные ссылки:

| Отчёт | Что даёт |
|---|---|
| `RE_VegasEffects.md` §4.2 («Родные плагины: ABI и загрузка») | ABI, `PluginFile`, `PluginInfo`, проверка пакета, типы, блок идентичности в `.rdata` |
| `RE_HFPL_Plugins.md` | формат файла `.hfpl` (PE32+, экспорты, сертификат, GLSL 410, категории, инвентарь 321) |
| `RE_Tannen_dll.md` §PluginManager/OFX | синглтон `PluginManager`, `OFXPlugin2DEffectWrapper`, песочница OFX, классы-обёртки |
| `RE_wnd_Effects.md` | панель Effects (дерево категорий, favorites/recents) |
| `RE_VegasEffects_Distribution.md` §5.1 | распределение плагинов по папкам |
| `SAMPLES/VEGAS_Effects/decompile-src/VegasEffects.exe/00_entry_crt/main_FUN_1401c2ce0_disasm.asm` | фактический вызов `PluginManager::Create` из EXE и пакет аргументов |

---

> ## ⚠ Поправка от 5 сентября 2026 — проверено вызовом всех 321 плагина
>
> Раньше этот отчёт строился только на декомпиляции. Теперь `PluginInfo` **реально вызван** у всех
> 321 модуля поставки (см. §11), и три вещи оказались неверны:
>
> 1. **Раскладка `PluginMetadata` (§4.3) была неправильной.** Строки лежат **не внутри** блока —
>    в блоке лежат **указатели на буферы, которые выделяет вызывающая сторона**. Отсюда и
>    «перекрывающиеся» смещения в старой таблице: 0x18/0x20/0x28/0x30 — это четыре подряд идущих
>    указателя, а 1024/600/200/800 — размеры буферов хоста. Исправленная таблица в §4.3.
> 2. **Ремап типа (§1, §4.3) перепутан у трёх из шести.** Правильно:
>    `0 → 2D, 1 → VideoTransition, 2 → AudioTransition, 3 → Audio, 4 → Geometry, 5 → Behavior`.
>    Проверено: у всех 321 тип совпадает с папкой.
> 3. **Версия пакета — 5.0 у всех 321**, а не «до 21.1.x». Проверка хоста `major < 22` относится к
>    версии *пакета плагина*, а не к версии приложения; §4.4 их смешивал.
>
> Заодно закрыт открытый вопрос §9.3: 16-байтный блок `+0x04` — это **GUID плагина**, уникальный
> (321 различное значение на 321 файл). Это и есть `fxtl::FXID`, по которому ищет `PluginByID`.

> **Дополнение от 20 сентября 2026.** `Notify` теперь также исполняется в порте. Восстановлены
> bootstrap-слоты API, 304/321 модулей проходят `Notify(0/1)`, все 241 2D-модуля проходят
> пиксельно проверенную GPU-цепочку `0 → 8 → 3 → 10 → 4 → 1`, а `Notify(2)` автоматически восстанавливает
> базовые контролы. Результаты и граница для остальных типов плагинов — в §11.3/§12.

---

## 0. Резюме (TL;DR)

VEGAS Effects держит **три независимых семейства плагинов**, но нативные
HitFilm-плагины (расширение `.hfpl`) — самое массовое (321 шт. в поставке).

Подсистему читает/управляет **`Tannen.dll`** (`biff::tannen::PluginManager`):

Декомпилят `VegasEffects.exe` подтверждает эту границу напрямую. По `0x1401c7993` EXE
вызывает импорт `biff::tannen::PluginManager::Create` (IAT `0x140be0060`), передавая
лицензионный менеджер, Marvin, три wide-строки, callbacks, native window handle и
`BiffHostEdition = 0x9c4`. Соседний импорт `0x140be0068` — `PluginManager::Log`, а не
функция окна. Поэтому таблицы ABI создаёт Tannen; EXE только формирует их зависимости.

1. **Скан диска** — `PluginManager::Load(dir, маски, recursive, excluded)`
   обходит каталоги `QDirIterator`'ом по маскам `*.hfpl` (HF) и `*.hfplx`
   (AE/послеэффектные), асинхронно, с отменой.
2. **Чтение метаданных** — для каждого кандидата `PluginFile` делает
   `LoadLibraryW` + `GetProcAddress("PluginInfo"/"Notify")`, зовёт
   `PluginInfo(host, api, &md)` → получает структуру `PluginMetadata`
   (тип, имя, категория, вендор, копирайт, magic `0x089E31C7`, версия, id).
   Пакет валидируется (`magic`, версия < 22), тип ремапится в один из шести,
   строится ключ `Effect_<tail-id>`. **Сразу после этого модуль выгружается**
   (`FreeLibrary`) — DLL в памяти не висит.
3. **Регистрация в модели** — вокруг каждого файла создаётся `shared_ptr`
   обёртки-наследника `AbstractPlugin` (по типу), кладётся в списки
   `Plugins()` и `PluginByID()`, показывается в панели Effects (Qt-дерево,
   категория — из метаданных, перевод через `QCoreApplication::translate`).
4. **Инстанциация при использовании** — когда эффект применяют к слою,
   модуль поднимается заново и вызывается **`Notify(host, api, 0)`**,
   который обязан вернуть 1 (иначе `"PluginLoaded returned error N"`). Далее
   весь обмен идёт по сообщениям `Notify` (протокол таблицы §6.3).
5. **Рендер/параметры/выгрузка** — параметры и контролы плагин объявляет сам
   через `tagBiffAPI` (хост: 1296 байт таблицы сервисов); значения передаются
   сообщением 10; рендер на GPU через шейдеры GLSL 410 (265/321 импортируют
   `opengl32`), CPU-классы `Behavior`/`Audio`, `MotionTrack` — OpenCV.

---

## 1. Три семейства плагинов

| Семейство | Формат | Классы в Tannen | Поставка (321) |
|---|---|---|---|
| Нативные HitFilm | `.hfpl` (PE32+ x64 DLL) | `Plugin2DEffect`, `PluginAudioEffect`, `PluginAudioTransition`, `PluginBehaviorEffect`, `PluginGeometryEffect`, `PluginVideoTransition` | 321 шт., 6 папок |
| Послеэффектные (AE) | `.hfplx` (архив AE) | `PluginAE2DEffect` | 0 шт. в поставке |
| OpenFX (openfx.org) | `.ofx.bundle` + песочница | `OFXPlugin2DEffectWrapper`, `OFXPluginTransitionWrapper` | не в `Plugins/` (системные каталоги) |

Тип плагина — **первое поле `PluginMetadata`** (см. §4.3). Ремапится хостом под класс-обёртку;
любое другое значение — исключение `"Invalid plugin type"`. Правильное соответствие (проверено
вызовом `PluginInfo` у всех 321 — тип модуля совпал с папкой поставки **во всех** случаях):

| Тип | Класс-обёртка            | Папка              | Файлов |
|----:|--------------------------|--------------------|-------:|
| 0   | `Plugin2DEffect`         | `2D`               | 241 |
| 1   | `PluginVideoTransition`  | `VideoTransitions` |  15 |
| 2   | `PluginAudioTransition`  | `AudioTransitions` |   2 |
| 3   | `PluginAudioEffect`      | `Audio`            |  16 |
| 4   | `PluginGeometryEffect`   | `Geometry`         |   4 |
| 5   | `PluginBehaviorEffect`   | `Behavior`         |  43 |

> Прежняя запись `0→2D, 3→Audio, 1→AudioTransition, 2→Behavior, 4→Geometry, 5→VideoTransition`
> **неверна** для 1, 2 и 5.

Папка в `Plugins/` задаёт класс-обёртку, а **категория в панели Effects —
не папка**, а строковое поле метаданных (§4.3, поле `+0x20`). Поэтому плоская
`Plugins/2D` (241 файл) разворачивается в 21 категорию.

---

## 2. Дискавери: скан каталогов

### 2.1 `PluginManager::Load` — @`0x3b22e0`

Mangled-сигнатура (из декомпила):

```
?Load@PluginManager@tannen@biff@@AEAAXAEBV?$basic_string@_W... /*dir* /
        AEBV?$vector<V?$basic_string@D...>>@ /*masks* / H /*recursive*/ /
        PEBV?$vector<VFXID@fxtl>>@ /*excluded*/
```

```c
void biff::tannen::PluginManager::Load(
        PluginManager* this,
        std::wstring const& dir,
        std::vector<std::string> const& masks,
        int recursive,
        std::vector<fxtl::FXID> const* excluded);
```

Что делает:

- использует **`QDirIterator` + `QDir`** для обхода каталога (стек-объекты
  `QDirIterator local_468`, `QDir local_458`), маски — `QString`;
- при отсутствии каталога — строка `"Directory does not exist: "` (ошибка);
- поддерживает **рекурсивный обход** с многоуровневым сбросом итераторов
  (категории лежат подпапками `Plugins/<Category>/…`);
- принимает **исключённые FXID** (`excluded`) — отсев уже известных/запрещённых;
- в теле есть специальная обработка `Curves2DPlugin` (исключение-тип),
  `AEPluginFile` и OFX-специфики (`.*IgnitePro.ofx.bundle`, mocha и т.п.) —
  общий сервис скана для всех трёх семейств;
- результаты складывает в вектор-список `Plugins()` суперобъекта
  (`*(this+8) + 0x1f8`) через `shared_ptr<AbstractPlugin>`.

### 2.2 Маски

Из кода и литералов `Tannen.dll`:

| Маска | Для кого | Литерал в DLL |
|---|---|---|
| `*.hfpl` | HitFilm-плагины | `Load.c` (`QString "*.hfpl"`) |
| `*.hfplx` | AE-плагины (`AEPluginFile::GetPluginPaths`) | файл `0x6a5268` |

В поставке расширений `.hfpl` — 321, `.hfplx` — **0** (`RE_HFPL_Plugins.md`).

### 2.3 Пути-в-памяти плагина и рестарт-поиск

Отдельные сервисные функции (`RE_Tannen_dll.md` §«Служебные глобальные
функции»):

- `GetPluginPaths` — формирует список каталогов для поиска `.hfplx`;
- `AuthorizePlugin`, `GlobalPluginProperties` — лицензирование/свойства;
- `LoadPlugin(path, vector<FXID>*, bool)` — публичная точка для одиночной
  догрузки (например, после установки нового плагина).

---

## 3. Асинхронность загрузки

### 3.1 Поток-скан

Сканирование выполняется **не на UI-потоке**:

- `PluginManager` хранит ленивый объект-loader + флаг готовности;
  доступ — двойной check с блокировкой (`_Mtx_lock`/`_Mtx_unlock`):
  `FUN_1803b0460(AbstractPluginManager*)` — мьютекс суперобъекта `+0x68`,
  loader-объект `+0x28`, флаг запуска `+0x108`, флаг завершения `+0x109`;
  запуск — вызов виртуала loader-объекта на `vtable+8` (запуск фонового
  `_beginthreadex`-потока, ctx-вызов + `_Cnd_do_broadcast_at_thread_exit`);
- **`WaitAEandHFPluginsToLoad`** @`0x3b0690` — «ждать, пока AE- и HF-плагины
  догрузятся»: разовая функция-барьер (поднимает флаг `+0x109`), блокирует
  до завершения скана; вызывается из UI-старта и из логики модели;
- `FUN_1803b0460` — тот же паттерн для «запустить скан, если не запущен»
  (используется из `PluginByID`, `ScopePlugins`, `WaitAEandHFPluginsToLoad`);
- отмена — глобальные `CancelPluginLoading` / флаг выключения `+0x6b0`
  проверяется в циклах `LoadOFXPlugins`.

### 3.2 Жизненный цикл состояния

```
первый доступ к плагинам
   └─» FUN_1803b0460: [lock]  start?  no  ──» loader->run()    (фоновый поток)
                            yes          флаг +0x108 = 1
UI / модель                 
   └─» WaitAEandHFPluginsToLoad: [lock]  finish? no ... wait
                            yes          флаг +0x109 = 1
```

---

## 4. Чтение метаданных: этап «считался с диска»

### 4.1 `PluginFile` и резолвер `FUN_180351c00`

`PluginFile` (наследник `AbstractPlugin`) — объект «файл плагина»; ctor
запоминает путь (`this+0x10`) и ленивый резолвер:

```c
if (this->hModule == 0) {
    h = LoadLibraryW(path);                 // иначе throw "LoadLibrary failed"  (0x6a50d0)
    this->hModule = h;
    this->pfnPluginInfo = GetProcAddress(h, "PluginInfo");   // this+0x40
    this->pfnNotify     = GetProcAddress(h, "Notify");       // this+0x48
    if (!pfnPluginInfo || !pfnNotify) throw "Missing API";   // 0x6b3720
}
```

Обе запрошенные функции живут в плагине как **первые два экспорта** (порядок
`Notify;PluginInfo`, см. `RE_HFPL_Plugins.md` §ABI).

### 4.2 Хост-прокладка

- **`BiffHost`** — 336 байт, сервисы хоста для плагина (строки хоста,
  версии, ID). Заполняется `FUN_180351cf0`.
- **`tagBiffAPI`** — 1296 байт, **таблица API** плагина←хост (создание
  контролов, рендер-контекст, стримы): `biff::tannen::PluginFile::CreateAPI(file, BiffHost&, tagBiffAPI&, 0)`.

### 4.3 Вызов `PluginInfo` → `PluginMetadata`

`ReadPluginMetadata` @`0x35adb0` вызывает точку входа:

```c
(**(code**)(*this + 0x40))(host, api, &metadata);   // pfnPluginInfo(BiffHost*, tagBiffAPI*, PluginMetadata*)
```

**Раскладка `PluginMetadata` — исправленная, проверена вызовом у всех 321 модуля.**

Ключевой момент, которого не было видно в декомпиле: **блок не содержит строк**. Он содержит
**указатели на буферы, которые выделяет вызывающая сторона**, а плагин копирует в них через
`wcsncpy`/`strncpy`. Именно поэтому в старой таблице смещения «перекрывались»: 0x18, 0x20, 0x28,
0x30 идут через 8 байт — это подряд лежащие указатели, а 1024/600/200/800/100 — размеры буферов
хоста, то есть 512/300/100/400 wide-символов и 100 байт ANSI.

Как это выяснилось: вызов `PluginInfo(nullptr, nullptr, зануленный_буфер)` падает
`0xC0000005` **внутри `ucrtbase!wcsncpy`**, `rdi = 0` (приёмник), `r8 = 0x200` (512 символов),
`rdx` указывает в `.rdata` самого плагина. То есть приёмник — то, что лежало в блоке, а не сам блок.

| Смещение | Тип в блоке | Поле | Что делает плагин |
|---|---|---|---|
| `+0x00` | `uint32` | `type` (0..5) | пишет |
| `+0x04` | 16 байт | **GUID плагина** | пишет; уникален — 321 значение на 321 файл (закрывает §9.3) |
| `+0x18` | `wchar_t*` | `name` | `wcsncpy(*(wchar_t**)(out+0x18), …, 512)` — **читает** указатель |
| `+0x20` | `wchar_t*` | `category` | `wcsncpy(…, 300)` |
| `+0x28` | `wchar_t*` | `vendor` | `wcsncpy(…, 100)` — `"FXhome"` у всех 321 |
| `+0x30` | `wchar_t*` | `copyright` | `wcsncpy(…, 400)` |
| `+0x38` | `uint32` ×4 | четыре флага | пишет; см. ниже |
| `+0x58` | `uint32` | **magic** | `0x089E31C7` — у всех 321 |
| `+0x5c` | `uint32` | версия major | `5` — у всех 321 |
| `+0x60` | `uint32` | версия minor | `0` — у всех 321 |
| `+0x68` | `char*` | `identifier` | `strncpy(*(char**)(out+0x68), …, 100)` |

**`PluginInfo` не трогает `host` и `api`.** У всех 321 модуля вызов с `nullptr` для обоих проходит,
если указательные слоты заполнены. Возвращаемое значение — 0 (у Invert); хост его не проверяет,
валидируя вместо этого magic и версию.

**Четыре флага `+0x38..+0x44`** — назначение по-прежнему не восстановлено, но распределение снято
со всех 321 (`hfpl_metadata.tsv`): `+0x38 ∈ {1,2}`, `+0x3c ∈ {0,1,2,3}`, `+0x40 ∈ {0,1}`,
`+0x44 = 0` всегда. Значение `2` в `+0x38` встречается ровно у десяти плагинов, и все десять —
2D-эффекты с экранными манипуляторами во вьювере (Chroma Key, Quad Warp, Puppet, Shake,
Action Cam Lens Distort, Custom Light Flares, четыре Lightsword). Корреляция отмечена, смысл не
доказан.

Пример реального ответа (`Invert.hfpl`):

```
type      = 0
guid      = 846382E5-F35E-4A2A-A55F820B96DF3B5E
magic     = 0x089E31C7
version   = 5.0
флаги     = 1 2 0 0
name      = "Invert"
category  = "Color Grading"
vendor    = "FXhome"
copyright = "Copyright (c) 2011-2022 FXhome Ltd. All Rights Reserved."
id        = "com.FXHOME.HitFilm.Invert"
```

### 4.4 Валидация пакета

Хост после вызова:

- **magic**: `0x089E31C7`;
- **версия** поля `+0x5c`: `major < 0x16` (22); если `major == 0x15` (21) — то
  `minor < 2`;
- иначе исключение:
  `"The plugin package information doesn't match the current build."` (файл `0x6b3730`).

**Поправка.** Это версия **пакета плагина**, а не приложения: все 321 плагина поставки сообщают
`5.0`, тогда как движок — 21.1.2. Порог `major < 22` (и `21 → minor < 2`) — это верхняя граница
формата пакета, до которой хост умеет читать; совпадение чисел с версией HitFilm 21.1 — совпадение
нумерации, а не одно и то же поле. В поставке все 321 плагина валидны с большим запасом.

### 4.5 Встраивание в модель: ключи и переводы

После чтения метаданных хост конструирует **ключ плагина**:

1. берёт `id` из `+0x68` (по умолчанию `PTR_1806a0628`, если плагин не дал id);
2. обрезает всё до последнего не-нулевого компонента после точки: хвост
   `com.FXHOME.HitFilm.Invert` → `Invert`;
3. склеивает с префиксом **`Effect_`** (литерал, 7 байт, файл `0x6a5250` рядом
   со спец-ключом `"Effect_Parent_Layer"`) или `"Effect_<...>"` — результирующий
   строковый ключ `Effect_Invert` кладётся в `PluginFile+0x1a0`;
4. имя/категория/копирайт переводятся штатным Qt-механизмом:
   `QCoreApplication::translate(context, source, …)` с контекстами
   **`Effect_<...>`** (перевод имени) и **`PluginStrings`** (литерал, файл
   `0x6b33a0`) — отсюда 321 контекст `Effect_*` в `.qm`-переводах
   `Translations/ja/*` (`RE_wnd_Effects.md`);
5. **Поправка.** `{`/`}` и `|` — это разные поля, а не оба категория:
   - **`{…}` стоит в `name`**: `"Chroma Key {greenscreen green screen bluescreen}"`,
     `"360° Blur {360° 2°° equirectangular equidistant}"`. В фигурных скобках — **ключевые слова
     поиска** панели Effects; 138 из 321 плагинов их несут. Отображаемое имя — часть до `{`.
   - **`|` стоит в `category`** и отделяет подкатегорию: `"Keying|Matte Enhancement"` (9),
     `"Transitions - Video|Dissolve"` (4), `"…|Motion"` (3), `"…|Wipe"` (3), `"…|Zoom"` (2) —
     всего 21 плагин из 321. Верхних категорий 26, полных строк категорий — 31.

**После всего этого модуль выгружается**: `FreeLibrary`. DLL в памяти держат
только для считывания метаданных; повторный `LoadLibraryW` произойдёт лишь на
этапе реального использования (§6).

---

## 5. Регистрация и управление обёртками

### 5.1 Контейнеры `PluginManager`

```
AbstractPluginManager (this+8, 0x6c8 байт)
 ├─*0x1f8…0x200  vector<shared_ptr<AbstractPlugin>>   Plugins()
 ├─*0x290…0x298  unordered_map<FXID, shared_ptr<AbstractPlugin>>   PluginByID()
 ├─*0x228…0x230  mutex (защита регистрации)
 ├─*0x68         mutex загрузки (скан)
 ├─*0x108/0x109  флаги запуска/завершения скана
 └─+0x418        OFXGlobalHost (для OFX-семейства)
```

- **`Plugins()`** — `FXList<shared_ptr<AbstractPlugin>>` (все загруженные);
- **`PluginByID()`** — публичная функция `PluginByID(FXID)` → `shared_ptr`
  (по ключу-идентификатору плагина, `Effect_` или FXID);
- `FindPluginsByType`, `ScopePlugins` — выборки по типу/области;
- защита — мьютекс, регистрация через `FUN_1803af620` (общий с OFX-путём).

### 5.2 Обёртки-наследники `AbstractPlugin`

| Тип (remap) | Класс | Политика рендера | Кол-во/папка |
|---|---|---|---|
| 0 | `Plugin2DEffect` | GPU (GLSL) | 241 / `2D` |
| 1 | `PluginAudioTransition` | CPU | 2 / `AudioTransitions` |
| 2 | `PluginBehaviorEffect` | CPU (35) / GPU (8) | 43 / `Behavior` |
| 3 | `PluginAudioEffect` | CPU | 16 / `Audio` |
| 4 | `PluginGeometryEffect` | GPU (1) / CPU (3) | 4 / `Geometry` |
| 5 | `PluginVideoTransition` | GPU | 15 / `VideoTransitions` |

(`RE_HFPL_Plugins.md` §профили; GL-профиль = импорт `opengl32.dll`.)

### 5.3 Показ и управление в UI

- панель **Effects** построена как категорийное дерево (`EffectsPanel`,
  `EffectsPanelTreeWidget`, `biff::ui::EffectsScreen` @`1412c3a30` в exe);
  узлы категорий — из поля `category` метаданных (не папки), операции
  favorites/recents — `RE_wnd_Effects.md`;
- поиск (`Keywords`), drag&drop на слой — MIME-типы панели;
- перевод узлов — через те же `Effect_<id>`/`PluginStrings` контексты §4.5;
- добавление эффекта на слой создаёт **`AbstractPluginInstance`** в проекте
  (модель), который на рендере ссылается на обёртку-плагин.

---

## 6. Инстанциация и работа: этап «встроен и работает»

### 6.1 `FUN_180351eb0` — поднять модуль под использование

Когда системе понадобился конкретный плагин (применение к слою, открытие
проекта с эффектом):

```c
if (flag-уже-инициализирован == 0) {
  Resolver(this);                       // повторный LoadLibraryW + два GetProcAddress
  BiffHost host;  BiffHost_ctor(&host);
  tagBiffAPI api; PluginFile::CreateAPI(this, &host, &api, 0);
  int r = this->pfnNotify(&host, &api, 0);       // message 0 = PluginLoaded
  if (r != 1)
     throw "PluginLoaded returned error " + r;   // 0x6b3770
}
```

`Notify(...,0)` и есть «регистрация модуля в хосте»: плагин **запоминает
глобально** указатели `host`/`api` (статические глобалы в своей секции) —
всю дальнейшую работу он ведёт через этот `tagBiffAPI`.

### 6.2 Протокол `Notify` (наблюдён на Invert.hfpl)

Сигнатура: `uint Notify(void* host, void* api, int message)`.

Карта сообщений **реального плагина** (декомпил `Notify` @`0x180009ec0`):

| message | Обработка | Примечание |
|---|---|---|
| `0` | `FUN_180013720(host, api)` → сохранить host/api в глобалы, инициализация модуля | вызывается хостом из `FUN_180351eb0`; ответ 1 = OK |
| `1` | `FUN_180013f20(api)` → освобождение ресурсов модуля | перед выгрузкой |
| `2`,`3`,`4` | `return 1` | «поддерживается / ack» (напр. capability-запросы) |
| `8` | печать `"Invert Plugin"` в `std::cout`; получение объекта плагина через `FUN_180011780(api)` и вызов его `vtable+0x28(ptr, ptr, apiflags[0x28])` | инициализация рендер-ресурсов (строки-имена шейдерных блоков) |
| `9`, `10` | 10: применение **значений параметров** (см. ниже) | 9 → default |
| `0x65`…`0x67` (101–103) | guarded: сначала `FUN_180011ed0(host,api)` — если хост «не умеет», вернуть **5**; иначе default → 2 | расширенный набор операций |
| `0x3EB` (1003) | guarded как выше | новое расширение |
| всё прочее | `return 2` | «неизвестное сообщение» |
| (guard-провал) | `return 5` | «хост не поддерживает» |

**Вывод о поведении по умолчанию:** базовый контракт `Notify` —
`0`=загрузка, `1`=выгрузка, `2..4`=ack, остальное=2, с группами расширенных
номеров (8–10, 101–103, 1003), вход в которые хост обязан «купить»
прохождением capability-проверки (иначе 5).

### 6.3 Параметры: кто их объявляет и как передаются

Контролы параметров создаёт **сам плагин** через `tagBiffAPI` (поэтому в
модуле лежат их ключи-строки и метки). В Invert (`case 10`):

```c
void* ctx = api->GetControlContext(api);             // FUN_180011780(api)
void* frame = *(void**)(api + 0x20);                 // ссылка на кадр/состояние
API_SetControl(ctx, 0);                              // FUN_180011100(ctx, 0)
uint keyA = API_FindKey(ctx, &"…параметр Name…");    // FUN_180011be0
API_SetValue(ctx, keyA, 4, 1, 0, *(uint64*)(frame+0x40));   // FUN_180010c60
uint keyB = API_FindKey(ctx, &"…");                  // FUN_180011be0
API_SetValue(ctx, keyB, 4, 1, 0, *(uint64*)(frame+0x48));
API_Commit(ctx, …);                                   // FUN_180010c40
```

Тип значения `4` (инт), флаги `1,0` — т.е. сообщение `10` = «протолкнуть из
модели проекта в контролы плагина». Классы контролов UI плагина —
`PluginUIButton`…`PluginUIString` (см. `RE_Tannen_dll.md` §«Конкретные
плагины»), создаются через те же API-вызовы.

### 6.4 Рендер

- **GPU-2D** (`2D`, `VideoTransitions`, часть `Geometry`/`Behavior`): плагин
  носит в себе вершинный+фрагментный шейдеры **GLSL `#version 410`**
  (у 265 из 321 файлов; всего 3585 вхождений `#version 410`), имена глобулов
  обфусцированы 32-hex. OpenGL-контекст плагину обеспечивает хост
  (захват WGL DC/Context в `FUN_1803aefb0`, `OFX::OpenGLContext` на выходе).
- **CPU** — `Audio*` (16+2) вообще без `opengl32`; `Behavior` смешанный;
  `MotionTrack` линкует `OpenCV 4.6` (`opencv_world460.dll`);
  `Text`/`Text3D` — `USER32/GDI32` + GDI+.
- Сервисы рендера хоста: `BeginRender/EndRender/Render`, `RenderManager`,
  `BeginBackgroundRender`, `GetScratchRenderBuffer` — перечислены в
  `RE_Tannen_dll.md`; пул сцены проекта подключается к инстансу плагина через
  `AbstractPluginInstance`.

---

## 7. Выгрузка и завершение

1. `Notify(api, 1)` — плагин освобождает свои ресурсы (рендер-буферы,
   шейдер-программы, OpenCV-объекты);
2. `PluginFile::~PluginFile` — `FreeLibrary(hModule)`;
3. `PluginManager::~PluginManager` — флаг выключения `+0x6b0`, мьютекс
   `0x1a8`, `glFinish`, создание `OFX::OpenGLContext`, **удаление временного
   каталога** (`QDir::removeRecursively` по пути от `DAT_1807692c8`);
4. «Отсутствующие плагины» проекта: `MissingPluginsToDeserialize` /
   `AddMissingPluginToDeserialize` — если в `.vegfx` есть эффект, которого
   больше нет на диске, модель помнит это и предупреждает при открытии.

---

## 8. Полный жизненный цикл (сводная схема)

```
 ┌─ PluginManager::Load(dir, ["*.hfpl"], recursive, excluded)   [фон. поток]
 │    QDirIterator по маскам; "Directory does not exist:" guard
 │
 ├─ PluginFile(path)
 │    ├─ Resolver : LoadLibraryW + GetProcAddress("PluginInfo","Notify")
 │    │              («LoadLibrary failed» / «Missing API»)
 │    ├─ BiffHost(336 B) + CreateAPI → tagBiffAPI (1296 B)
 │    ├─ pfnPluginInfo(host, api, &md) → PluginMetadata (таблица §4.3)
 │    ├─ validate: magic==0x089E31C7, major<22 (21→minor<2)
 │    │            «The plugin package information…»
 │    ├─ remap type → wrapper-class; «Invalid plugin type» иначе
 │    ├─ key "Effect_" + tail(id)  → контекст перевода QCoreApplication
 │    │            + «PluginStrings» (копирайт); категория { } |
 │    └─ FreeLibrary                        ← модуль временно выгружен
 │
 ├─ register: shared_ptr<AbstractPlugin-наследник> → Plugins() vector
 │            + PluginByID() map (mutex); UI: панель Effects (категории)
 │
 ├─ применение: FUN_180351eb0
 │    ├─ Resolver (повторный LoadLibraryW)
 │    ├─ BiffHost + CreateAPI (снова)
 │    └─ pfnNotify(host, api, 0) == 1  или «PluginLoaded returned error N»
 │
 ├─ работа:  Notify 2/3/4 (ack), 8 (инициал. рендер), 10 (параметры),
 │           101-103/1003 (расширения), значения через tagBiffAPI
 │           рендер: GLSL 410 (GPU) / CPU / OpenCV (MotionTrack) / GDI+ (Text)
 │
 └─ завершение: Notify(1) → FreeLibrary → деструктор PluginManager
                (temp-каталог удаляется; missing-plugins в проекте)
```

---

## 9. Открытые вопросы

- Точный состав каталогов скана (детали `GetPluginPaths`/AE) — из этого
  декомпила видны маски и механика, но не полный список папок.
- Для `Notify(0/1)` и GPU-пути `Notify(8/10)` подтверждён исполняемый стенд
  (§11.3): `Invert.hfpl` проходит 0 → 8 → 10 → 1 в OpenGL 4.1 offscreen-контексте.
  Открыт точный порядок сообщений `101+` и различия для Audio/Transition.
- ~~Назначение 16-байтного блока `+0x04`~~ — **закрыт**: это GUID плагина (`fxtl::FXID`),
  321 уникальное значение на 321 файл; см. §4.3 и `hfpl_metadata.tsv`. Про
  `"UnitTests-UI.exe"` в декомпиле: это соседний литерал в `.rdata`, а не содержимое блока —
  реальный GUID пишется плагином в рантайме. Назначение четырёх интов `+0x38..+0x44` остаётся
  открытым (распределение снято, см. §4.3).
- Полная карта `tagBiffAPI` (1296 байт). Bootstrap и первые GL-сервисы уже
  восстановлены (`+0x40`, `+0x48`, `+0x1f8`, `+0x4a8..+0x4b8`), остаются
  контролы, дополнительные входы, аудио и переходы.

---

## 10. Справочник (адреса и файлы)

### Tannen.dll (база `0x180000000`)

| Функция / литерал | Адрес / файл-offset |
|---|---|
| `PluginManager::Load` | `0x3b22e0` (`Load.c`) |
| `WaitAEandHFPluginsToLoad` | `0x3b0690` |
| start-scan guard `FUN_1803b0460` | `0x3b0460` |
| `ReadPluginMetadata` | `0x35adb0` |
| резолвер `FUN_180351c00` | `0x351c00` |
| load-for-use `FUN_180351eb0` | `0x351eb0` |
| `"LoadLibrary failed"` | файл `0x6a50d0` |
| `"Effect_"` / `"Effect_Parent_Layer"` | файл `0x6a5250` |
| `"*.hfplx"` (маска AE) | файл `0x6a5268` |
| `"PluginStrings"` (контекст) | файл `0x6b33a0` |
| `"Missing API"` | файл `0x6b3720` |
| `"The plugin package information doesn't match…"` | файл `0x6b3730` |
| `"PluginLoaded returned error "` | файл `0x6b3770` |
| `"Invalid plugin type"` | файл `0x6b3998` |
| `"Directory does not exist: "` | файл `0x6b9930` |

### Invert.hfpl (база `0x180000000`, экспорты)

| Функция/литерал | Адрес |
|---|---|
| `Notify` (экспорт) | `0x180009ec0` |
| `PluginInfo` (экспорт) | `0x18000a190` |
| метаданные: `FUN_18000a480/470/3c0/490` | `0x18000a480/470/3c0/490` |
| wide-литерал у 16-байт блока | `0x18003b6f8` (`"UnitTests-UI.exe"…`) |

### Провенанс

- `C:\Users\Admin\AppData\Local\Temp\opencode\decompile_addrs.py` — скрипт
  декомпила по адресам (PyGhidra); лог `hfpl_helpers_py_log.txt`.
- Ghidra-проект `D:\ghidra_projects\HFPL\HFPL.gpr` (Invert.hfpl, полный
  анализ, 994 функции).
- Декомпил Tannen: `decompile-src\tannen_decomp_full\*.c`.

*Отчёт составлен: RE-сессия VEGAS Effects, файлы `.hfpl`, движок плагинов.*

---

## 11. Что подтверждено вызовом реальных плагинов (5 сентября 2026)

### 11.1. Метод

Отдельный процесс-зонд (`hfpl_runtime_probe`, ранее `hfpl_probe.cpp`/`hfpl_all.cpp`) делает то же, что `PluginFile`:
`LoadLibraryEx` → `GetProcAddress("PluginInfo")` → вызов → `FreeLibrary`. Вызов обёрнут в SEH,
поэтому падение плагина сообщается, а не роняет процесс. Новый зонд также держит постоянные
`BiffHost`/`tagBiffAPI`, вызывает `Notify(0/1)` и умеет посылать произвольную цепочку сообщений.

Прогон по `SAMPLES/VEGAS_Effects/Plugins`: **321 из 321** модулей отдали метаданные.

Первый прогон дал 317/321: `MotionTrack`, `AutomaticStabilizer`, `MotionLock` и `LightFlaresV2`
не грузились. Причина не в контракте, а в зависимостях — им нужен `opencv_world460.dll` и ещё один
модуль из папки приложения, то есть **уровнем выше** `Plugins/`. С добавлением этой папки в путь
поиска модулей (`AddDllDirectory`) — 321/321.

### 11.2. Результаты (полный дамп — `RE_HFPL_Plugins/hfpl_metadata.tsv`, 321 строка)

| Проверка | Результат |
|---|---|
| `PluginInfo` вызван успешно | **321/321** |
| `magic == 0x089E31C7` | **321/321** |
| версия пакета | **5.0 у всех 321** |
| тип модуля совпадает с папкой поставки | **321/321** |
| уникальных GUID | **321** (по одному на файл) |
| уникальных идентификаторов | **320** — `DepthMask.hfpl` и `DepthMatte.hfpl` оба `…HitFilm.DepthToMatte` |
| вендор | `FXhome` у всех 321 |
| идентификатор ≠ имени файла | **59 из 321** (`BoxBlur→Blur`, `Flame→Fire`, `FlyEye→InsectVision`, все Lightsaber→Lightsword, …) |
| имён с ключевыми словами `{…}` | 138 |
| категорий с подкатегорией `|` | 21 |
| верхних категорий | 26 |

Стоимость чтения: **~58 мс на модуль**, почти целиком `DllMain` самого плагина, то есть
**~18.6 с на 321**. Это и объясняет, зачем референс уводит скан в фоновый поток (§3).

### 11.3. Исполняемый `Notify` и восстановленные слоты API (13 сентября 2026)

`NativePluginRuntime` выделяет блоки того же размера, что референс: `BiffHost=336` и
`tagBiffAPI=1296` байт. Адреса не меняются до выгрузки. Восстановлены bootstrap-слоты:

| Смещение API | Сигнатура/назначение | Подтверждение |
|---:|---|---|
| `+0x00` | указатель на вложенную таблицу legacy-сервисов; используются как минимум `+0x08` и `+0x18` | `Levels` читает `[api[0]+8]`; `ChromaKey`, `ColorCorrection`, `Derez` читают `[api[0]+0x18]` во время `Notify(2)` |
| `+0x10` | module-specific instance render state, создаваемый `Notify(3)` и освобождаемый `Notify(4)`; legacy fallback для модулей без сообщения 3 | scopes хранят здесь task/cache state, `Levels` читает индекс sample/frame |
| `+0x20` | `tagBiffRenderContext*`, записывается `PluginFile::Render` перед `Notify(10)` | подтверждено дизассемблированием `Tannen.dll` RVA `0x35a810` |
| `+0x40` | `const wchar_t* HostName(BiffHost*)` | Clone/GoPro/LightFlaresV2/Puppet сравнивают имя продукта |
| `+0x48` | `int HostEdition(BiffHost*)` | BIFF edition: `0x9c4` (2500) для VEGAS Effects, `0x898` (2200) для HitFilm |
| `+0x1f8` | `int PackageInfo(BiffHost*, {magic,major,minor}*)` | 360Glow и ещё 7 модулей требуют `089E31C7/5/0` до создания объекта |
| `+0xb0` | `ComboBoxValue` | точная карта `PluginFile::CreateAPI` |
| `+0xb8` | `FloatValue`/`AngleValue` | оба имени экспортируют один ABI чтения `float` |
| `+0xc0` | `BoolValue` | точная карта `PluginFile::CreateAPI` |
| `+0xc8` | `IntValue`/`ComboBoxValue` | общий ABI чтения целого/индекса |
| `+0xd8` | `AngleValue` | используется сложными 2D-модулями |
| `+0xe0` | `ColorValue` | RGB через три выходных `float*` |
| `+0xe8` | `LayerID` | записывает идентификатор layer-параметра в 40-байтовый буфер |
| `+0xf0` | `GetLayerInfo` | сведения о выбранном слое; image-only путь возвращает `-5` |
| `+0xf8` | `GetScratchTexture` | width/height/формат, возвращает GL texture и фактические размеры |
| `+0x108` | `GetTimelineInfo` | размеры, pixel aspect, длительность, FPS и аудиоформат timeline |
| `+0x120` | `ClearReservedTexture` | снимает резервирование scratch texture в renderer pool |
| `+0x178` | `NotifyProgress(BiffHost*, long, long)` | возвращает признак отмены; без callback отмены — `0` |
| `+0x1e8` | `GetNumberOfKeyframes` | image-only путь получает уже вычисленное значение кадра и сообщает 0 сырых keyframes |
| `+0x278` | `SetStringValue` | обновляет строковое значение внутри текущего render-вызова |
| `+0x2c8` | `RedrawCustomUI` | подтверждает запрос инвалидации custom control; фактическая перерисовка остаётся в Qt GUI-thread |
| `+0x2d0..+0x2e0` | mask collection | обычный медиаслой сообщает 0 масок |
| `+0x2e8/+0x2f0` | text outline collection | обычный медиаслой сообщает пустой контур текста |
| `+0x350` | `HostOptions` | референсный capability mask `1` |
| `+0x4e8` | `MaskIndex` | чтение индекса MaskPicker |
| `+0x4a8` | `GetVAO` | вызывается общим 2D render-helper перед отрисовкой |
| `+0x4b0` | `GetScratchVBO(BiffHost*, target, byteCount, data, usage, actualByteCount*, cache)` | возвращает зарезервированный GL buffer и его фактический размер |
| `+0x4b8` | `ClearReservedVBO(BiffHost*, id)` | снимает резерв; ошибка исходного API для неизвестного id — `-15` |
| `+0x500` | `GetScratchRenderBuffer(BiffHost*, context, width, height, internalFormat, precision, cache)` | резервирует/переиспользует GL renderbuffer |
| `+0x508` | `ClearReservedRenderBuffer(BiffHost*, context, id)` | снимает резерв; ошибка неизвестного id — `-16` |
| `+0x60` → `+0xb8` | регистрация / чтение scalar `float` | `BrightnessContrast`, `Gamma`, `Threshold` |
| `+0x70` → `+0xc0` | регистрация / чтение `bool` | `FindEdges.isInverted` |
| `+0x78` → `+0xc8` | регистрация / чтение enum/int | `Threshold.source` |
| `+0x90` → `+0xe0` | регистрация / чтение RGB | `Threshold.color1/color2`, `Fill.fillColor` |
| `+0x220` → `+0xb8` | float slider с отдельным display range / чтение `float` | `360Blur.radius`, `VignetteExposure.amount` |
| `+0x228` → `+0xc8` | int slider с отдельным display range / чтение `int` | 10 контролов поставляемого набора |
| `+0x230` → `+0xc0` | action Button / импульсное чтение `bool` | tracking, crop, denoise и plugin options actions |
| `+0x238` | динамический Label | статус анализа и вычисленные значения |

`Plugin2DEffect::Render` (vtable `+0x178`, RVA `0x333890`) строит
`tagBiffRenderContext` на стеке и передаёт его в `PluginFile::Render`. Для обычного
2D пути подтверждены: `+0x08` — указатель на 36-символьный UUID source, `+0x10/+0x18` — входной/выходной `tagBiffTexture*`, `+0x24` —
коэффициент pixel aspect/нормализации, `+0x2c/+0x30` — scale, `+0x38..+0x58` — пять
указателей `float*` на transform matrices, `+0x60/+0x64` — ширина/высота кадра.
`tagBiffTexture` занимает как минимум `0x34` байта: GL name лежит в `+0x00`, texture
target (`GL_TEXTURE_2D`) и pixel format — в `+0x04/+0x08`, размеры allocation — в
`+0x0c/+0x10`, видимой области — в `+0x14/+0x18`, UV rectangle — в
`+0x1c..+0x28`, коэффициенты масштаба — в `+0x2c/+0x30`. Нулевой выходной указатель объяснял прежние падения
`ColorCorrection`, `ChromaKey` и `Derez`.

Массовый прогон `Notify(0)` по поставке:

| Этап | Успешно |
|---|---:|
| нулевая API-таблица | 251/321 |
| добавлен `PackageInfo +0x1f8` | 259/321 |
| добавлен ошибочный product build `6000` в `HostEdition +0x48` | 302/321 |
| добавлен `HostName +0x40` | **304/321** |

Оставшиеся 17 не падают: они штатно возвращают **7 (несовместимый хост)** в раннем
прогоне, где `+0x48` ошибочно трактовался как product build 6000. Все Audio (16), AudioTransitions (2), Behavior (43), Geometry (4),
VideoTransitions (15) и 224 из 241 2D-модулей проходят загрузку; исключения локализованы в 2D.

Для GPU нужен активный GL-контекст. Зонд создаёт offscreen OpenGL 4.1 Core, после чего
`Invert.hfpl` успешно выполняет `Notify(8)` (компилирует встроенные GLSL 410) и `Notify(10)`
(читает frame block, привязывает input texture, выставляет uniforms и рисует). Для входа
`10,20,30,255` readback даёт ожидаемый инвертированный пиксель `245,235,225,255` без GL-ошибок.
Тот же общий путь подтверждён ещё на 240 модулях; пиксельно проверены все 241/241:
`Invert`, `InvertAlpha`, `AlphaBrightnessContrast`, `BrightnessContrast`, `ChannelMixer`,
`ChannelSwapper`, `ColorBalance`, `ColorConverter`, `ColorCorrection`, `ColorTemperature`, `CrushBlacksWhites`,
`Demult`, `Exposure`, `ExposurePro`, `Fill`, `FindEdges`, `Gamma`, `Threshold`, `Tint`,
`WhiteBalance`, `Levels`, `ChromaKey`, `Derez`, а также `ColorDifferenceKey`, `DuoTone`,
`HueShift`, `Posterize`, `ThreeStripColor`, `TwoStripColor`, `Vibrance`,
`360Blur`, `360ChannelBlur`, `360FisheyeConverter`, `360Glow`, `360GlowDarks`,
`BezierWarp`, `Bulge`, `ChromaticAberration`, `Crop`, `FisheyeWarp`, `PolarWarp`,
`Vignette`, `VignetteExposure`, а также `AutoColor`, `AutoContrast`, `AutoLevels`,
`BleachBypass`, `CineStyle`, `ClassicCineStyle`, `ColorVibrance`, `CrushBlacksWhitesAlpha`,
`CustomGray`, `DayForNight`, `Emboss`, `HSL`, `HueColorize`, `HueKey`, `NeonGlow`,
`RemoveStockBackground`, `ShadowHighlight`, `SpillSuppressor`, `YUVColorCorrection`.
Пространственный прогон также подтвердил `AngleBlur`, `BilateralBlur`, `BoxBlur`,
`ChromaBlur`, `Diffuse`, `EdgeDistortion`, `FlyEye`, `HighpassSharpen`, `LensBlur`,
`Magnify`, `Mosaic`, `RadialBlur`, `Sharpen`, `Twirl`, `Unsharpen`, `WarpVortex`,
`Waves` и `ZoomBlur`.
После реализации пустого layer/camera/light контекста добавлены `FisheyeWarp2`, `Sphere`,
`PageCurl`, `PerspectiveWarp`, `PiP`, `PondRipple`, `Projector`, `QuadWarp`, `Reflection`
и `RollingShutter`.
После приведения выходного FBO к цветовому target без неявного combined depth/stencil
добавлены `Tiles` и `Wireframe`: последний подключает собственную scratch depth-текстуру,
не конфликтующую с оставшимся stencil attachment.
Без дополнительных host services также подтверждены `4PointRamp`, `Cartoon`, `ColorPhase`,
`DotMatrix`, `DropShadow`, `FilmGrain`, `GlowDarks`, `Grain`, `HalfTone`, `Letterbox`, `Noise`,
`PencilSketch`, `RadialGradient`, `Ramp` и `YUVColorTransform`.
Контрастный тест также подтвердил `Dehaze`, `Deinterlace`, `Glow`, `LeaveColor`,
`LuminanceKey`, `OilPainting`, `ScanLines`, `Solarize` и `ToneColoring`.
Последняя изолированная матрица добавила 42 модуля, включая temporal-эффекты
`FrameBlendedRetiming`, `FreezeFrame`, `MotionBlur`, `TimeDisplace`, `TimeWarp`, генераторы
и scopes `AudioSpectrum`/`AudioWaveform`. Параметризованные эффекты проверены минимум двумя
наборами значений на одном runtime либо ненулевым значением против известного входа, затем тем же
вызовом из отдельного render-thread.

Следующая матрица восстановила capability byte `tagBiffAPI+0x28`, mask/text-outline,
`MaskIndex`, keyframe-count и string-setter services. Она включила `LUT`, `Stroke`,
`VectorStroke`, `LightFlaresV2`, auto/path-варианты lightsaber, `AtomicParticle` и
`PulpScifiTitleCrawl`. Для path editor фильтр отделён от пустого значения свойства, а
multiline capture хранит полный XML preset LightFlaresV2 вместо первых 511 символов.

Финальная матрица добавила `360Text`, `AutomaticStabilizer`, `CreditsTextCrawl`, `Histogram`,
`MotionLock`, `Puppet`, `Text`, `Vectorscope`, `Waveform` и `WaveformParade`. Для них
`RenderContext+0x08` получает стабильный source UUID, `Notify(3)` создаёт module-specific
instance state, `Notify(4)` освобождает его до выгрузки DLL, а `RedrawCustomUI +0x2c8`
подтверждает асинхронный запрос custom control. Старые модули, не реализующие сообщение 3,
продолжают использовать постоянный минимальный instance block.

Отдельный прогон восстановил ABI `PluginVideoTransition::Render`. В отличие от 2D-эффекта,
контекст перехода содержит первый и второй входы в `+0x10/+0x18`, выходной target в `+0x20`,
матрицы в `+0x38/+0x40`, размер в `+0x48/+0x4c` и sample/start/end в
`+0x54/+0x58/+0x60`. `NativeEffectRender` теперь принимает две разные картинки и нормализованную
позицию перехода, не подменяя второй вход выходной текстурой. Все 15 поставляемых
`VideoTransitions` проходят `Notify(8/10)`, readback и совпадают в основном и отдельном
render-thread; `CrossDissolve` при позиции 0.5 даёт точное среднее двух контрольных цветов.

Для `Behavior` декомпиляция `PluginFile::TransformationAtTime`, `OpacityAtTime` и
`SimulateBehavior` разделила сообщения `102/104/103` и capability `6/7/9`. Рабочий путь сейчас
исполняет frame-вызовы 102/104: общий префикс контекста несёт layer ID и timeline/local frame,
а `+0x18` является соответственно указателем на матрицу 4x4 либо inline opacity. В API
подключена группа запросов состояния слоя `+0x300..+0x330`; `PositionMix` и `RotateByLayer`
дополнительно подтвердили её вызовами. Двухпоточная матрица совпала для 20 Behavior-модулей;
они применяются `RenderManager` после пиксельных эффектов и до масок/композитинга. Буфер BIFF
копируется прямо в column-major storage `QMatrix4x4`, без транспонирования его row-major
конструктором.

Декомпиляция `Project.dll::CompositionAsset::SimulateLayers` дала полный базовый интегратор
`Notify(103)`. `tagBiffBehaviorSC` хранит index `+0x00`, time/step `+0x18/+0x20`, callback state
`+0x28`, массив layer-state `+0x30`, acceleration/force callback `+0x40` и damping callback
`+0x48`. Mode 0 накапливает acceleration, mode 1 — нормализуемый force; после вызова всех
Behavior хост применяет `pow(damping, dt)`, обновляет velocity и затем position. Runtime использует
частоту текущей композиции и воспроизводит этот порядок. `Acceleration` и `Gravity` дают
соответственно 186 и −258.333 px за первую секунду при 30 fps, совпадают между потоками.
Runtime собирает `Acceleration`, `Gravity` и `Drag` в один покадровый проход с общей скоростью:
при значениях по умолчанию их совместный результат `(171.958, -238.831, 0)` одинаков в основном
и worker-thread. Все три включены в production whitelist; вместе с `AttractTo`,
`Follow` и `RepelFrom`, проверенными с layer-state массивом, и `Throw` с
нулевым началом симуляции итог этой ветки — 26/43 Behavior. Отдельный
`Notify(105)` исполняет 16 текстовых модулей с массивом 0x5c-byte записей символов;
`Typewriter`, `DropInByChar` и `StringFade` подключены к посимвольным opacity и
2D-матрицам в TextRender и повышают production-итог до 29/43. Время для этого
контекста передаётся в миллисекундах; ClipValue состоит из двух флагов и четырёх
float. Для остальных 13 модулей ещё требуется применение ClipValue и проверка
геометрии символов. `MotionTrack` удалён из whitelist после повторной проверки:
он отвергает все четыре runtime-callback.

Выяснено, что SDK хранит в каждом модуле compatibility sentinel: если исходная проверка
окружения оставляет его равным `-1`, `Notify` всё равно возвращает успех, но uniform setters
становятся no-op. `NativePluginRuntime::enableRenderingCompatibility()` находит этот DWORD по
повторным RIP-relative ссылкам из исполняемой секции (RVA `0x5a440` у Invert) и активирует уже
проверенный ABI-путь, не меняя файл на диске.

---

## 12. Порт: что реализовано

`src/plugin/NativePlugin.{h,cpp}`, `src/plugin/PluginManager.cpp`.

**Двухступенчатая проверка, как у референса.** Сначала статический разбор PE: оба экспорта должны
быть на месте — это гейт `"Missing API"`. Только прошедший гейт файл получает вызов `PluginInfo`.
Так чужой код не запускается для файла, который заведомо не плагин.

**Настоящие метаданные.** `loadNativePluginMetadata()` реализует контракт §4.3: выделяет пять
буферов, кладёт указатели в слоты `0x18/0x20/0x28/0x30/0x68`, зовёт `PluginInfo(nullptr, nullptr, …)`
под SEH, читает тип, GUID, magic, версию, флаги и строки, затем `FreeLibrary`. Папка над каталогом
скана добавляется в путь поиска модулей (`AddDllDirectory`/`RemoveDllDirectory` — cookie, а не
`SetDllDirectory`, чтобы не менять глобальное состояние процесса).

**Валидация — референсная.** Magic, `major < 22` (и `21 → minor < 2`), тип 0..5; отказ логируется
формулировкой оригинала.

**Разбор строк.** `{ключевые слова}` отрезаются от имени и попадают в поиск панели Effects;
`Категория|Подкатегория` разделяется; `identifier` даёт `shortName` и контекст перевода
`Effect_<Id>`.

**Жизненный цикл.** `NativePluginRuntime` владеет DLL, dependency-directory cookie и постоянными
host/API-блоками. Он устанавливает три подтверждённых bootstrap-сервиса, вызывает `Notify(0)` и
гарантированно посылает `Notify(1)` перед `FreeLibrary`. Оба вызова защищены SEH; код и адрес
ошибочного доступа попадают в диагностику. `tools/hfpl_runtime_probe.cpp` проверяет отдельный файл
или всё дерево и умеет исполнять дополнительные сообщения в скрытом OpenGL 4.1-контексте.

**Кэш.** Чтение 321 модуля — 18.6 с; повторный скан читает JSON-кэш v10
(`%LOCALAPPDATA%/…/plugin-metadata.json`), ключ — путь + размер + время изменения файла. Измерено:
**18576 мс → 14 мс**. Неудачное чтение не кэшируется — вдруг недостающая зависимость появится.

**Текущая граница UI.** У `EffectSpec` есть `renderable` и `unavailableReason`.
`NativeEffectRender` хранит разрешённый при сканировании путь модуля, создаёт постоянный
OpenGL 4.1 context/runtime на render-thread, загружает GLSL через `Notify(8)`, а на каждом кадре
передаёт input texture, две матрицы и размеры через frame block и читает FBO после `Notify(10)`.
Восстановленные value services читают значения по ASCII-ключам параметров и поддерживают scalar,
bool, enum/int, RGB, Button/Label, UTF-16 string/path, MaskPicker, Point2D/Point3D, Angle и Orientation. Включены все 241 пиксельно проверенных 2D-модуля, 15/15 видео-переходов,
16/16 аудиоэффектов и 2/2 аудиоперехода; их `EffectParameterSpec`
воспроизводит имена, диапазоны, единицы, значения по умолчанию и варианты выбора из `Notify(2)`.
Во время первичного скана `Notify(2)` автоматически строит и кэширует спецификации базовых
контролов: `+0x50/+0x58` дают группы, `+0x60` float, `+0x68` int, `+0x70` bool, `+0x78` enum с
UTF-16 списком через `|`, `+0x80/+0x88` Point2D/Angle, `+0x90` RGB,
`+0x220/+0x228` float/int с отдельным display-range,
`+0x230/+0x238` Button/Label,
`+0x240..+0x260` single-line, multiline и path, `+0x338/+0x370` Orientation/Point3D.
Для action-кнопок подключён `SetBoolValue +0x1a0`; установленное DLL значение действует
для последующих чтений в текущем `Notify`/render-вызове.
Подключены безопасные 2D-ответы `GetLayerInfoV2 +0x128`, `GetActiveCamera +0x138`,
`GetCameraInfo +0x140`, `GetMotionBlurInfo +0x148` и `GetActiveLights +0x158`: отсутствие вторичного слоя, камеры, motion samples и света представляется теми же
пустыми структурами/счётчиками, которые возвращает Tannen для соответствующего контекста.
`GetLayerTexture +0x100`, `GetSourceTexture +0x118` и `GetLayerTextureV2 +0x130`
возвращают value-copy полного `tagBiffTexture` текущего входа из `RenderContext+0x10`;
`GetLayerDepthTexture +0x2f8` очищает выход и возвращает `-4` для 2D-источника без depth plane.
На полном каталоге получено 5403 контрола у 241/241
2D-модулей. Все 241 вызов `Notify(2)` завершаются:
для `ChromaKey`, `ColorCorrection`, `Derez` и `Levels` реализован указатель `tagBiffAPI[0]`
на вложенную legacy-таблицу сервисов (`+0x08`/`+0x18`). Кэш метаданных поднят до версии 10.
SEH по-прежнему защищает скан от повреждённых сторонних модулей. Полная матрица рендера
подтвердила 241/241 результатов в основном и отдельном render-thread.

Рабочие input/output targets соответствуют compositor ABI: `GL_RGBA32F` с компонентами
`GL_FLOAT`. Постоянный scratch pool повторно использует свободную текстуру только при совпадении
размера, internal format, pixel format и component type; `ClearReservedTexture` снимает резерв,
а фактическое удаление выполняется при уничтожении GL-контекста render-thread. Перед выделением
очищается унаследованный `GL_PIXEL_UNPACK_BUFFER`, поскольку много-проходные модули оставляют PBO
привязанным и `nullptr` в `glTexImage2D` иначе становится смещением в чужой буфер.
Аналогичный постоянный пул реализован для `GetScratchVBO`: совместимый свободный VBO сохраняет
storage и получает новые вершины через `glBufferSubData`; `ClearReservedVBO` больше не вызывает
`glDeleteBuffers` посреди много-проходного кадра. Точная семиаргументная сигнатура и код ошибки
`-15` подтверждены `Tannen.dll::PluginHostAPI::GetScratchVBO/ClearReservedVBO`.

`QOffscreenSurface` создаётся в GUI-thread, как требует Qt, после чего используется контекстом
render-thread. `RenderManager` явно освобождает thread-local native renderer до `QThread::quit()`;
это исключает создание Qt GL-объектов TLS-инициализатором до `QGuiApplication` и взаимную блокировку
при остановке worker. Если два потока держат один модуль, compatibility sentinel уже равен `1`;
поиск принимает это состояние наравне с исходным `-1`.

**Аварийный выход.** `OPENVEGAS_NO_NATIVE_PLUGIN_LOAD=1` полностью отключает загрузку модулей —
остаётся статический разбор `.rdata`, как было раньше.

**Проверено** headless-тестом `plugin_test.cpp` — 48 проверок против настоящих 321 модуля:
контракт на одном модуле, ключевые слова и подкатегории, полный прогон (321 загрузок, magic, GUID,
совпадение типа с папкой, распределение по типам), регистрация в менеджере, ускорение кэша и работа
переключателя отключения.
