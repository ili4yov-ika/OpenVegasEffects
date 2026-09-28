# RE_UI_Stubs — аудит заглушек интерфейса

Обновлено: 2026-09-28. Объект проверки: `src/ui`, потребители настроек в `src/app`,
`src/media`, `src/project` и `src/render`; оригинал сопоставлен через подключённый
Ghidra MCP. Этот документ заменяет аудит 2026-09-14: старые номера строк и утверждения
о неработающем звуке больше не соответствовали коду.

Настройка считается реализованной, когда она меняет поведение приложения, а не только
проходит `loadSettings → saveSettings`. Поиск ключа в OptionsDialog сам по себе этого
не доказывает. Проверки сборки и автоматические тесты отделены от проверки оборудования.

Повторный аудит 2026-09-27: живые string/xref/decompile запросы Ghidra для
VegasEffects.exe, проверка соответствующих функций в локальном bulk-декомпиляте
и поиск потребителей в текущем `src`. Найдены дополнительные незавершённые пути:
Turbo Rendering, подключение Media Cache DB к медиапайплайну
и realtime native audio DSP. Последний теперь подключён для Audio `.hfpl` на уровне
клипа; лимит thumbnail cache и многодорожечный master mix также подключены. Предыдущее описание этих частей
как полностью рабочих исправлено. Миграция libVLC и подключённые обработчики
описаны ниже; остальные незавершённые пути остаются в матрице раздела 4.

Статусы в этом документе:

- **Реализовано:** действие подключено к модели/рендеру/сервису и имеет наблюдаемый результат.
- **Частично:** часть пути работает, но не весь заявленный элемент интерфейса.
- **Только хранение:** виджет, load/save/reset или Settings getter есть, потребитель отсутствует.
- **Не установлено:** имеющихся данных Ghidra недостаточно для точного алгоритма/формата оригинала.

Совпадение текста кнопки, наличие одноимённого XML-поля и вызов `QPainter::Antialiasing`
не доказывают применение соответствующей настройки Options.

## 1. Что подтверждено в Ghidra

| Адрес/функция VegasEffects.exe | Наблюдение | Применение в нашем коде |
|---|---|---|
| `1412c7bd8`, `FUN_140235ca0` | Чтение bool `Options/UseNativeColorPicker` | Общий `interfaceColor` в Theme.h читает настройку при каждом открытии |
| `FUN_140235d80` | Запись настройки и вызов `ColorBlockWidget::SetUseNativeColorPicker` | Все редакторы цвета используют один helper, включая Text, параметры эффектов, Layer и Viewer |
| `1412c7d00`, `FUN_140236280` | Чтение `Options/ShowMenuBarQuickActions` | `MainWindow::applyInterfacePreferences` управляет `widgetQuickActions` |
| `FUN_140236360` | При изменении настройки вызывается `QMetaObject::activate` | В нашем UI настройка повторно применяется после закрытия Options, а также после загрузки раскладки |
| `141317090`, `FUN_140769470` | Диалог `Record Voiceover` создаёт `QAudioRecorder`, читает `Options/VoiceoverVolume`; элементы имеют свойство `stop-playback` | Запись PCM через libVLC `AudioCapture`, управление транспортом, входной уровень |
| `1412c7408`, `FUN_140769070` | Перечисление `QAudioRecorder::audioInputs`, чтение `Options/VoiceoverDevice` | Options перечисляет DirectShow/CoreAudio/Pulse inputs; используются строковые ID. Старые Qt base64 ID требуют повторного выбора устройства |
| `FUN_140769d50` | Сохранение выбранного `VoiceoverDevice` | Наш текущий совместимый с предыдущими сборками ключ устройства — `Options/Voiceover/Device` |
| `1412c7438` | Строка `VoiceoverPath` | Последний каталог успешной записи хранится в `Options/VoiceoverPath` |
| `1412e5918`, `1412e5880` | Имена `checkBoxSaveScreenLayout`, `checkBoxRelativePaths` | Соответствующие флажки теперь влияют на сериализацию проекта |
| `141313b80` | Команда `Create Voiceover Recording`, xref `FUN_14071a640` | Меню Record и редактируемая горячая клавиша `Ctrl+Shift+R` открывают запись |
| `1412c76b0`, `FUN_140231840`, `FUN_1403a48a0` | Чтение integer `Options/AudioWaveformStyle` с fallback 2 и запись из General Options | Наши `Options/AudioWaveforms` хранят текст выбора; алгоритм waveform не подключён. Числа native enum не сопоставлены с RMS/Peak |
| `1412c76c8`, `FUN_140231920`, `FUN_1403a48a0` | Integer `Options/WaveformScaleType`, fallback 1 | Наш `Options/LogWaveform` — bool, пока только сохраняется; прямую совместимость INI не заявляем |
| `FUN_140665b10`, `14130a870`, `14130a930` | Проверка `MediaAudioStream::WaveformPreviewAvailable`, чтение peak-file path, опциональных `CacheLayerWaveforms`/`LayerWaveformChunks`, отправка `WaveformPreviewIsReady` | Нужен отдельный peak/cache/image pipeline. Audio Meters, RMS буферов AudioPlayer и эффект `.hfpl` Waveform его не заменяют |
| `1412c7ce0`, `FUN_1402361a0`, `FUN_1403b36d0` | Bool `Options/ShowProjectSettingsDialog`: getter и сохранение Prompts | New Project читает `Options/Prompts/ShowProjectSettings` и при включённом флаге открывает свойства композиции |
| `FUN_1401dc5b0` → `FUN_1402361a0` | Xref вызова; bulk-декомпилят подтверждает условную ветку создания окна при включённом флаге и режиме, отличном от 5000 | Не считать реализацией одно наличие флажка в Options; полный смысл режима 5000 пока не установлен |
| `1412c7418`, `FUN_14022f5e0`, `FUN_1402a7140` | Bool `Options/EnableEffectPresetCreation`, default false, getter и setter | Наш `Debug/EffectPresetCreation` изменяет только сохранённый флаг |
| `FUN_14041e430` → `FUN_14022f5e0` | Xref; в соответствующем bulk-декомпиляте флаг включает путь `PluginUIPreset::SavePropertyName`/`DeletePropertyName` | Это дополнительный режим работы native UI preset, а не доказательство отсутствия обычных пользовательских presets у нас |
| `1412c76e0`, `FUN_140231760`, `FUN_1403c0930` | Bool `Options/ProxyPreferIntegratedGPU`; запись вместе с `ProxyDirectoryPath`, integer `ProxyMediaQuality`, `PreRenderDirectoryPath` | Наши ProxyQuality/PreferIntegratedGPU сохраняются, но создание прокси и выбор GPU отсутствуют |
| `FUN_1403a48a0`, `1412c7328`, `1412c7340` | General Options записывает строки `CompositionLength`/`EditorSequenceLength`; также `SequenceObjectDefaultImageLength`, `SavePathsAsRelative`, `AutoSleepWakeAssets`, `AnalyticsReportingEnabled` | Наши ключи и представления отдельно сопоставлены ниже; наличие native setter не раскрывает весь backend |
| `1412c7770`, `1412c78a8`, `1412c78c0` | Определённые строки `TimelineCacheDB`, `TimelineCacheFiles`, `DaysToKeepTimelineCacheFiles` | Уровень доказательства здесь — имена, а не восстановленный алгоритм дискового кеша |
| `FUN_1408cf1b0`, `FUN_1408cec90`, `FUN_140381170`, `FUN_140380080` | Выбор слоя для Layer; Settings и повторное распознавание направления 360 по углам | Подробные результаты и ограничения: [Layer](RE_wnd_Layer.md), [360 Viewer](RE_wnd_Viewer360.md) |

Строка в бинарнике подтверждает наличие имени, но не формат данных или полный алгоритм.
Параметры Channels/SampleRate/Countdown/MuteOutput являются настройками нашей записи;
не заявляется, что они восстановлены из оригинала. Раскладка `OpenVegasScreenLayout`
и расширения клипов относятся к OpenVegas, а не к восстановленной нативной схеме.

Для двух больших функций (`FUN_1401dc5b0`, `FUN_14041e430`) живой запрос
декомпиляции не вернул текст. Xref проверен в Ghidra; условные ветки дополнительно
прочитаны из `SAMPLES/VEGAS_Effects/decompile-src/VegasEffects.exe/10_bulk_addr/`.
Их нельзя представлять как вновь полученный полный live-декомпилят.

### 1.1 Ключи оригинала и нашего приложения

Все имена оригинала ниже относятся к группе `Options`; наши полные ключи также
начинаются с `Options/`, кроме явно указанного Debug. Это сопоставление назначения,
а не разрешение копировать INI без преобразования типов и enum.

| Оригинал | Наш ключ | Состояние |
|---|---|---|
| `AudioWaveformStyle` (integer) | `AudioWaveforms` (текст выбора) | Только хранение; enum mapping не восстановлен |
| `WaveformScaleType` (integer) | `LogWaveform` (bool) | Только хранение |
| `CompositionLength` (строка) | `CompositeShotDefaultDuration` | Применяется при создании композиции; native формат строки отдельно не проверен |
| `EditorSequenceLength` (строка) | `EditorDefaultDuration` | Только хранение |
| `SequenceObjectDefaultImageLength` | `PlaneDefaultDuration` | У нас применяется к Plane; не доказана эквивалентность для всех native image assets |
| `SavePathsAsRelative` | `UseRelativePaths` | Рабочее расширение сериализатора OpenVegas |
| `AutoSleepWakeAssets` | `CloseMediaOnInactive` | Освобождаются ресурсы MainWindow при неактивности; полный lifecycle оригинала не восстановлен |
| `AnalyticsReportingEnabled` | `Analytics` | Только хранение; `Debug/PrintAnalytics` — отдельный локальный журнал |
| `ShowProjectSettingsDialog` | `Prompts/ShowProjectSettings` | New Project читает флаг и открывает свойства при включении |
| `ProxyMediaQuality` (integer) | `ProxyQuality` (текст выбора) | Только хранение; enum mapping не установлен |
| `ProxyPreferIntegratedGPU` | `PreferIntegratedGPU` | Только хранение |
| `EnableEffectPresetCreation` | `Debug/EffectPresetCreation` | Только хранение; наш ключ находится в группе Debug |
| `ShowHardwareDecodingIndicator` | `Debug/HardwareDecodingIndicator` | Только хранение; достоверного индикатора активного декодера нет |
| `TimelineCacheDB`, `TimelineCacheFiles`, `DaysToKeepTimelineCacheFiles` | `TimelineCache`, `TimelineCacheDays` | Неполное сопоставление: у нас нет отдельной пары database/files с native схемой |

Точные строки `DefaultTemplate`, `EditorDefaultDuration`, `RenderThreads`,
`LogWaveform` в этой выборке Ghidra не найдены. Отрицательный string search не
доказывает отсутствия соответствующей функции в оригинале. Default Template у нас
представлен тремя встроенными строками; native `AVTemplate`/библиотека шаблонов
не восстановлены этим аудитом.

## 2. Исправленные заглушки

| Элемент/ключ | Реальное действие и место реализации |
|---|---|
| Record Voiceover | `VoiceoverDialog.cpp`: запись микрофона, обратный отсчёт, остановка, сохранение WAV, отмена; `MainWindow.cpp`: добавление отдельного слоя в позицию начала записи с Undo/Redo |
| `Options/Voiceover/Device` | Устройство выбирается из `QMediaDevices::audioInputs`; отключённое устройство выдаёт ошибку, а не заменяется произвольным микрофоном |
| `Options/Voiceover/Channels`, `/SampleRate` | Проверяются `isFormatSupported`, используются в формате PCM; неподдерживаемая комбинация не начинается молча с другими параметрами |
| `Options/Voiceover/Countdown` | Таймер перед запуском записи и транспорта, отмена во время отсчёта безопасна |
| `Options/Voiceover/MuteOutput` | На время записи заглушает выход `AudioPlayer`, затем снимает заглушение |
| `Options/VoiceoverVolume`, `/VoiceoverPath` | Слайдер регулирует PCM gain в `AudioCapture::setVolume`; запоминается последний каталог сохранённой записи |
| `Options/UseNativeColorPicker` | Все вызовы диалога цвета заменены общим helper; false включает `DontUseNativeDialog`, true разрешает системный диалог |
| `Options/UseNativeMenuBar` | Применяется через `QMenuBar::setNativeMenuBar`; фактический внешний вид зависит от платформы |
| `Options/ShowMenuBarQuickActions` | Показывает/скрывает панель быстрых действий при запуске и изменении Options |
| `Options/EnableWheelScrollMenus` | Глобальный фильтр блокирует изменение закрытого QComboBox колёсиком при false; открытый список сохраняет обычную прокрутку |
| `Options/HideFullScreenPreview` | Полноэкранный preview скрывается при неактивном приложении и возвращается при активации |
| `Options/CloseMediaOnInactive` | При деактивации останавливает транспорт/scrub, освобождает аудиоисточник и принадлежащие MainWindow видеодекодеры |
| `Options/PlayAudioOnScrub` | При остановленном транспорте проигрывает короткий фрагмент в позиции scrub; таймер остановки отменяется при обычном Play |
| `Options/UseRelativePaths` | `ProjectSaveOptions`, обычное сохранение и autosave: Filename, пути моделей и MediaID/ModelAssetID в расширениях сохраняются относительно проекта; при загрузке разрешаются относительно его каталога |
| `Options/IncludeScreenLayout` | Сохраняет Qt dock-state как base64 в `OpenVegasScreenLayout`; при открытии применяется с проверкой версии Qt state и восстановлением нижних dock-углов/меню |
| `Options/Labels/%1/Name`, `/Color` | Меню меток таймлайна предлагает восемь сохранённых имён/цветов и отдельный произвольный цвет; изменение использует общий Undo/Redo; заблокированный слой не редактируется |
| `Options/RemoveExtensions` | ExportPanel использует имя композиции; удаляет известное исходное расширение из основы имени, сохраняя расширение выбранного формата экспорта |
| Export Frame при пресете MP4/MOV | Один кадр экспортируется в PNG; Export Contents продолжает использовать выбранный видеоформат. Ранее image.save пытался записать кадр как MP4 |
| `Debug/PrintAnalytics` | Включает локальные записи `UI event` для зарегистрированных QAction; внешняя телеметрия не отправляется |
| Browse Tutorials в Learn | Вызывает рабочую команду Online Help вместо сообщения об отсутствии встроенных уроков |
| Timeline `renderRequested`/`optionsRequested` | Удалены неиспользуемые сигналы и соединение. Рендер по scrub/изменению модели и команда Render Frame остаются рабочими |
| Viewer `m_texts`, `m_textDraft`, `m_textPlaceView` | Удалён мёртвый рисующий путь. Текст создаётся и редактируется через реальные слои композиции и TextRender |

Изменение палитры Options не перекрашивает ранее размеченные слои: меню применяет новый
цвет только к выбранному слою/медиа. MediaPanel использует ту же палитру, произвольный цвет и No Label; старые метки не перекрашиваются при изменении палитры.

### Запись WAV

`media/PcmWave.h` записывает RIFF/WAVE, signed 16-bit PCM, заданные частоту и число каналов.
При остановке неполный последний sample frame отбрасывается. Запись идёт во временный
PCM-файл; окончательный файл публикуется через QSaveFile только после успешной упаковки.
Cancel и ошибка не перезаписывают существующий WAV. Ошибка устройства останавливает
таймер и транспорт и отображается в диалоге. Запись с настоящего микрофона требует
доступного устройства и разрешения ОС; её аппаратная проверка в этой сессии не выполнялась.

### Относительные пути

Проверяются не только Filename, но и ссылки в `OpenVegasClips` и во вложенных композициях.
Идентификаторы загруженных клипов сопоставляются с импортированными путями. Прямой
относительный путь, включая `../`, разрешается до поиска перемещённых файлов по имени.
Если файл отсутствует, его абсолютный путь относительно нового проекта сохраняется
для Relink Media. Несколько совпадений не приводят к выбору произвольного файла.

## 3. Ранее реализованное, которое не следует считать заглушкой

- AudioPlayer использует libVLC для декодирования и системного вывода PCM.
  Активные клипы нескольких видимых, незаглушённых слоёв суммируются с учётом
  sourceStart, speed, gain и вложенных композиций. Audio Meters получает RMS
  того же ограниченного итогового PCM после master mute. Очереди декодеров
  ограничены, stop/seek отменяют ожидания и устаревшие queued-сигналы.
  Нативные Audio `.hfpl` теперь действуют на PCM клипа до gain и master mix,
  параметры `.hfpl` пересчитываются на каждом 10-мс блоке по keyframes, но
  AudioTransition и поблочная автоматизация финального экспорта ещё не подключены;
  скорость меняет высоту тона через ресемплирование.
- Маски и Behaviors в Controls используют общие редакторы и Undo/Redo. Разделение
  UI-параметров Behaviors не означает завершённое исполнение закрытого ABI всех `.hfpl`.
- Text: реальные слои, форматирование, цвета, Point/Paragraph, редактирование на канвасе.
- Vector Path создаёт Freehand mask/path через рабочий путь таймлайна.
- Рабочие пути: History/Undo, Layout, Trimmer, Viewer 360, Media/Relink, сохранение проекта,
  экспорт области, кеш воспроизведения, докинг, горячие клавиши и Options load/save/reset.
  Это перечень существующих обработчиков, а не утверждение о полном совпадении с VEGAS.
- MaxUndo, Theme, Language, High DPI, длительность новой композиции/Plane,
  2D checkerboard, motion path/координаты, playback update, quality/downsample profiles,
  autosave, snapshot/export directories и beep уже имеют потребителей.
  Media Cache DB имеет startup-потребитель, но его связь с генерацией кеша неполная;
  ThumbnailCacheSizeMB задаёт лимит QPixmapCache для повторно используемых миниатюр Media. Память QIcon видимых элементов не входит в этот лимит.

### 3.1 Частичные пути, ранее описанные слишком широко

| Путь | Что действительно работает | Чего нет |
|---|---|---|
| Audio preview / Audio Meters | libVLC PCM master mix всех активных Audio/Video клипов: visible/muted, offset, speed, gain, вложенные композиции; native Audio `.hfpl` перед gain с 10-мс keyframe-автоматизацией, seek/loop и RMS итогового PCM | Realtime AudioTransition, поблочная автоматизация экспорта; сохранение высоты тона при speed |
| Видеоэкспорт со звуком | Отдельный путь `MainWindow::finishVideoExport`: FFmpeg `atempo`, `volume`, `adelay`, `amix`; native Audio обрабатывается перед сведением | Этот экспортный путь не подключён к AudioPlayer и master meter при Play/Scrub |
| Media Cache DB | `AppMain::initializeCache`: каталог, SQLite open, подсчёт записей, `pruneOlderThan` по сроку хранения | Вне CacheDB нет использования `put/get` для наполнения и повторного использования media cache. `MediaManager` держит отдельный RAM video-frame cache с фиксированным лимитом 256 MiB |
| Thumbnail cache limit | QPixmapCache использует ThumbnailCacheSizeMB; миниатюры Media повторно используются по path/mtime/size, QImageReader декодирует уменьшенное изображение | QIcon видимого списка хранит свои ссылки; это не общий лимит всей памяти Media |
| Кнопка кеширования timeline | `preRenderRequested` запускает/отменяет `RenderManager::startPlaybackCache` | Нет дискового pre-render, TTL timeline cache и автоматического запуска по задержке |
| 3D project settings | Serializer записывает `AntialiasingMode=0`, `ReflectionMapSize=512`, `ModelTextureMaxSize=4096`, `ShadowMapSize=2048` | Это константы, а не применение значений Options к 3D renderer |

## 4. Оставшиеся заглушки — пока не закрыты

Layer/360 Viewer дополнительно исследованы в Ghidra и реализованы:
[Layer](RE_wnd_Layer.md), [360 Viewer](RE_wnd_Viewer360.md). Layer получил
отдельный preview выделенного слоя; 360 — исправленные повороты, Roll,
рабочий camera FOV и отмену Properties без изменения состояния.
Wrap No/Tile/Reflect восстановлены через Notify(2) EnvironmentMapViewer.hfpl.
Native render pipeline, lens model и Fisheye/Scale/Motion Blur пока не восстановлены полностью.

| Группа | Настройки/элементы без полного потребителя | Что требуется |
|---|---|---|
| Waveforms | `AudioWaveforms`, `LogWaveform` | Построение и отображение waveform с выбором линейной/логарифмической шкалы |
| Analytics | `Options/Analytics` | Отдельная политика аналитики; Debug/PrintAnalytics реализует только локальное журналирование действий |
| Proxies | `ProxyDirectoryPath`, `ProxyQuality`, `PreviewMode`, `PreferIntegratedGPU` | Создание/выбор прокси и согласование его с оригинальным источником |
| Pre-Renders | `PreRenderDirectoryPath` | Кеш на диске; существующий playback cache хранит кадры в памяти |
| Prompts & Warnings | `Options/Prompts/*`, кроме подключённого ShowProjectSettings | Подключение остальных флагов к импорту, GPU/QuickTime, камерам и удалению export tasks |
| Render | `TurboRendering`, планировщик `RenderThreads`, `UseHardwareEncoding`, `LimitVideoDecodingTo8bit` | UseHardwareDecoding и avcodec-threads уже передаются видеодекодеру VLC; фактическое hardware acceleration требует проверки и возможен fallback при CPU readback. Настройки encoder/renderer ещё не подключены |
| Media cache | `MediaCacheDB`, `MediaCacheFiles`, `DaysToKeepMediaCacheFiles` | Startup open/prune работает; требуется наполнение и повторное использование кеша медиапайплайном |
| Realtime audio DSP | Native AudioTransition | 16 Audio `.hfpl` подключены к PCM master mixer, проверены на смене keyframe `Balance.hfpl`; модель перекрытий ещё отсутствует |
| 3D Render/Display | `ModelTextureMaxSize`, `ShadowMapSize`, `ReflectionMapSize`, `Antialiasing`, `ShowCheckerboard3D`, `ShowFloorPlane` | Соответствующие рендер-пути, текстуры и настоящая 3D сцена Viewer |
| Timeline cache | `TimelineCache`, `TimelineCacheDays`, `UseAutomaticRenderCache`, `RenderCacheDelay` | Хранилище и автоматическое планирование; ручной playback cache не заменяет эти опции |
| Export | `TimeFormat` | Применение к представлению времени export tasks, а не произвольное изменение имён файлов |
| Debug | `HardwareDecodingIndicator`, `EffectPresetCreation` | Достоверный статус используемого декодера и отдельный debug-путь создания presets; обычные пользовательские presets уже работают |
| Models | Строки `Models`/nodeNames | Сохранение связи треугольников с узлами, затем per-node visibility/transform/selection; сейчас mesh плоский и строка выбирает весь слой |

Перечисленные опции могут сохраняться в INI, но это не делает их реализованными.
Чеклист верхнего уровня остаётся незавершённым. Само наличие строки или widget-name в
Ghidra не является достаточным основанием считать backend восстановленным.

### 4.1 Критерии закрытия следующих пунктов

| Приоритет | Пункт | Наблюдаемая проверка |
|---|---|---|
| 1 | Realtime audio DSP | Native эффект меняет PCM при Play; переход обрабатывает оба перекрывающихся клипа, автоматизация соответствует времени композиции. Базовые сумма/mute/gain/offset/speed/seek уже проверены на PCM |
| 1 | Waveform | Из PCM создаются пики и изображение, RMS/Peak и linear/log дают ожидаемые различия; масштаб и retime соответствуют таймлайну; отмена/недоступное медиа не оставляют старый waveform |
| 1 | Prompts | Установка/снятие конкретного флага меняет соответствующий путь New/Import/Delete; вне данного prompt остальные подтверждения сохраняются |
| 2 | Proxy/pre-render/cache | Созданный файл реально выбирается для preview; cache hit не повторяет декодирование; инвалидируются изменения медиа/параметров и применяется TTL |
| 2 | Render/GPU/3D options | Проверяется используемый backend и результат, включая fallback при отсутствии поддержки; XML-константы и сохранение флажка не закрывают пункт |
| 2 | Thumbnail cache | Изменение лимита меняет фактическую удерживаемую память/eviction при множестве миниатюр |
| 3 | Export time | Переключение TimeFormat меняет именно представление export tasks |
| 3 | Models | Действие адресует конкретный node, меняет модель, проходит Undo и save/load; метки медиа уже проверены отдельно |

### 4.2 Повторная live-проверка Ghidra и границы миграции

2026-09-27 через MCP получены live-декомпиляты `FUN_1408cf1b0` и
`FUN_140381170` в VegasEffects.exe. Первый подтверждает No/Multiple Selection,
проверку renderable asset и передачу единственного слоя в `FUN_1408cec90`.
Второй открывает Properties, читает `envTrasnformXRot/YRot/ZRot` (опечатка
оригинала), повторно определяет направление через `FUN_140380080`, отправляет
changed и обновляет QWidget. Эти результаты подтверждают UI-контракты,
но не раскрывают GPU EffectInstance, lens model или Fisheye/Motion Blur.

Qt Multimedia удалён из CMake, qmake, AudioPlayer, Voiceover и тестов.
Заголовки VLC 4 берутся из thirdparty/vlc, готовый runtime задаётся отдельно:
дерево исходников нельзя копировать вместо библиотек и плагинов.
Vlc4Adapter проверяет ABI, адаптирует parser/callback structs, асинхронную
остановку и единицы времени; adapter собран MSVC/MinGW, но runtime проверен
только на Windows VLC 3.0.24. Linux/macOS и VLC 4 требуют запуска на целевой ОС.
AudioCapture использует DirectShow/qtsound/PulseAudio; новая запись сохраняет
WAV через прежний атомарный путь. Физическая запись микрофона не проверена.

### 4.3 Закрытые потребители 2026-09-28

- Media labels: меню Media использует восемь имён/цветов Options, Custom color
  и No Label. Цветная полоса сохраняется в режиме списка и миниатюр; offline
  media сохраняет также красное предупреждение. Undo/Redo адресует asset ID,
  не текущий индекс; смена менеджера и закрытие панели делают старую команду
  безопасной. Успешный New/Open очищает историю исходящего проекта.
- ImageAsset и MediaAsset сохраняют RGBA метку в собственном атрибуте
  OpenVegasLabelColor. Недоступные файлы получают метку после registerMissingFile;
  Relink переносит её и trimmer points. Это расширение OpenVegas; native формат
  меток asset не восстановлен. Live Ghidra подтверждает имена Labels
  (1412cf384), Set Clip Label (1412deb38), Set Layer Label (1412dfcb0),
  но эти строки не доказывают формат сериализации метки медиа.
- DefaultTemplate: три шаблона UI реально задают size/fps нового проекта.
  Постоянные ID fullhd30/fullhd60/uhd30 хранятся в DefaultTemplateId;
  старое отображаемое имя читается при миграции. Смена языка не сбрасывает ID.
- EditorDefaultDuration применяется к frameCount/outPoint EditorSequence
  независимо от CompositeShotDefaultDuration. Новый проект и первый запуск
  используют общий helper; открытые проекты сохраняют собственные параметры.
  Отдельный полноценный native editor timeline этим изменением не реализован.
- Settings::optionSettings объединяет соответствующие потребители с INI,
  который записывает OptionsDialog. На Windows прежний QSettings() читал
  реестр: исправлены палитра timeline/media, native color picker, wheel menus,
  Voiceover, scrub/inactive audio, hardware/thread decoder options, quick actions,
  свойства нового проекта и флаги сохранения/автосохранения.
- Загрузка .vegfx разбирает композицию/медиа/раскладку во временные объекты
  и заменяет текущие данные только при успехе. Save использует QSaveFile,
  проверяет write/commit и не обрезает существующий проект до завершения записи.
  Это защита от ошибок I/O; полное сохранение всех неизвестных native полей
  остаётся открытым пунктом основного чеклиста.

## 5. Проверки

Этап 2026-09-28: CMake/MSVC Debug и qmake/MSVC Debug собраны; после правок сериализации 6/6 CTest прошли. Затем отдельно проверены компактные строки Media с метками и снимок панели.

- После миграции CMake/MSVC Debug и qmake/MSVC Debug с Qt 6.9.3 собраны без Qt Multimedia. qmake staging проверен: обе DLL, 366 plugin files и лицензии скопированы, включая путь VLC с пробелами. CMake install перенёс runtime в bin/vlc; NSIS inventory проверен на настоящем runtime. NSIS compile проверен на небольшом fixture, пакет без VLC отвергается. Инсталляция на чистую ОС не выполнялась.
- Дополнительные регрессии: mediaLabelsUndoAndOfflineRoundTrip (RGBA/offline/index shift/No Label/manager change/panel destruction), projectDefaultsUseStableTemplateAndEditorDuration, failedProjectLoadPreservesCurrentState, templateSelectionSurvivesChangedDisplayText и colorPickerUsesOptionsIniStore.
- `audio_regression`: реальный VLC player и PCM мастер-выхода, сумма двух источников с gain, взаимная компенсация, mute/unmute, отложенный клип, sourceStart/speed на неоднородном сигнале, seek и отсутствие сигналов после stop.
- `ui_stubs_regression`: структура WAV и содержимое PCM, неполный sample frame,
  пустой/неподдерживаемый PCM, Cancel с существующим файлом, прокрутка QComboBox,
  имена экспорта и разные форматы Export Frame/Export Contents.
- `timeline_regression::relativePathsAndWorkspaceSurviveProjectMove`: перенос проекта,
  несколько клипов, вложенная композиция, отсутствующее медиа, включение/выключение
  относительных путей и layout round-trip.
- После миграции повторно прошли все шесть CTest-наборов: Text, Timeline, Options, UI Stubs, Audio и Translations (6/6); после дополнений 360 отдельно прошли Timeline/Translations и тест миграции настроек.
- `tools/validate_translations.py`: новые сообщения переведены на ru/ja/zh_CN;
  при повторном аудите проверено по 1001 актуальному сообщению, пустых и unfinished нет.
- Для актуализации аудита проверены string/xref/decompile связи, реальные потребители
  ключей и Settings getter, отсутствие обращения `CacheDB::put/get` вне реализации CacheDB,
  новый master mixer AudioPlayer и отдельный экспортный `amix`.

Offscreen-тесты не проверяют настоящий микрофон, аудиоустройство, системный native
color dialog или взаимодействие с оконным менеджером при смене активного приложения.
