# RE_UI_Stubs — аудит заглушек интерфейса

Обновлено: 2026-10-04. Объект проверки: `src/ui`, потребители настроек в `src/app`,
`src/media`, `src/project`, `src/render` и `src/model3d`. Эта редакция сверяет документ
с текущими исходниками и [чеклистом](CHECKLIST.md); новых live-запросов Ghidra в ней
не выполнялось. Адреса и результаты Ghidra ниже относятся к предыдущим исследованиям,
включая live-аудит 2026-09-27 и восстановление Layout 2026-10-03.

Настройка считается реализованной, когда она меняет поведение приложения, а не только
проходит `loadSettings → saveSettings`. Поиск ключа в OptionsDialog сам по себе этого
не доказывает. Наличие обработчика, результаты тестов и проверка оборудования —
разные уровни подтверждения.

Исправлены противоречия прежней редакции: Qt Multimedia уже заменён libVLC;
waveform, ProxyQuality/PreferIntegratedGPU, EditorDefaultDuration и export TimeFormat
имеют потребителей. Шаблоны обслуживает AVTemplates, экспорт — ExportJob/AudioExport.
Остаются ограничения realtime native audio, Media Cache DB, render/GPU/3D options,
масштаба превью и визуальных границ Layout; они перечислены в разделах 3.1 и 4.

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
| `1412c76b0`, `FUN_140231840`, `FUN_1403a48a0` | Чтение integer `Options/AudioWaveformStyle` с fallback 2 и запись из General Options | Наши `Options/AudioWaveforms` хранят английский ключ выбора (старый переведённый текст читается), таймлайн рисует RMS или Peak. Числа native enum не сопоставлены с RMS/Peak |
| `1412c76c8`, `FUN_140231920`, `FUN_1403a48a0` | Integer `Options/WaveformScaleType`, fallback 1 | Наш `Options/LogWaveform` — bool, переключает шкалу −60…0 dBFS/линейную в waveform таймлайна; прямую совместимость INI не заявляем |
| `FUN_140665b10`, `14130a870`, `14130a930` | Проверка `MediaAudioStream::WaveformPreviewAvailable`, чтение peak-file path, опциональных `CacheLayerWaveforms`/`LayerWaveformChunks`, отправка `WaveformPreviewIsReady` | Порт: `media/AudioWaveform` — фоновое FFmpeg-декодирование в mono 8 kHz, пики и RMS по 10 мс, память и дисковый peak-кэш по пути/размеру/mtime/выбранному аудиопотоку; `TimelineCanvas` рисует столбцы видимой части клипа с учётом slip/speed. Раскладка native peak-файла не восстановлена |
| `1412c7ce0`, `FUN_1402361a0`, `FUN_1403b36d0` | Bool `Options/ShowProjectSettingsDialog`: getter и сохранение Prompts | New Project читает `Options/Prompts/ShowProjectSettings` и при включённом флаге открывает свойства композиции |
| `FUN_1401dc5b0` → `FUN_1402361a0` | Xref вызова; bulk-декомпилят подтверждает условную ветку создания окна при включённом флаге и режиме, отличном от 5000 | Не считать реализацией одно наличие флажка в Options; полный смысл режима 5000 пока не установлен |
| `1412c7418`, `FUN_14022f5e0`, `FUN_1402a7140` | Bool `Options/EnableEffectPresetCreation`, default false, getter и setter | Наш `Debug/EffectPresetCreation` изменяет только сохранённый флаг |
| `FUN_14041e430` → `FUN_14022f5e0` | Xref; в соответствующем bulk-декомпиляте флаг включает путь `PluginUIPreset::SavePropertyName`/`DeletePropertyName` | Это дополнительный режим работы native UI preset, а не доказательство отсутствия обычных пользовательских presets у нас |
| `1412c76e0`, `FUN_140231760`, `FUN_1403c0930` | Bool `Options/ProxyPreferIntegratedGPU`; запись вместе с `ProxyDirectoryPath`, integer `ProxyMediaQuality`, `PreRenderDirectoryPath` | MainWindow::setAssetProxyMode читает ProxyQuality и PreferIntegratedGPU; ProxyGenerator запускает FFmpeg, проверенный h264_qsv используется при предпочтении integrated GPU, иначе libx264. Native enum и общий выбор GPU не восстановлены |
| `FUN_1403a48a0`, `1412c7328`, `1412c7340` | General Options записывает строки `CompositionLength`/`EditorSequenceLength`; также `SequenceObjectDefaultImageLength`, `SavePathsAsRelative`, `AutoSleepWakeAssets`, `AnalyticsReportingEnabled` | Наши ключи и представления отдельно сопоставлены ниже; наличие native setter не раскрывает весь backend |
| `1412c7770`, `1412c78a8`, `1412c78c0` | Определённые строки `TimelineCacheDB`, `TimelineCacheFiles`, `DaysToKeepTimelineCacheFiles` | Уровень доказательства здесь — имена, а не восстановленный алгоритм дискового кеша |
| `FUN_1408cf1b0`, `FUN_1408cec90`, `FUN_140381170`, `FUN_140380080` | Выбор слоя для Layer; Settings и повторное распознавание направления 360 по углам | Подробные результаты и ограничения: [Layer](RE_wnd_Layer.md), [360 Viewer](RE_wnd_Viewer360.md) |
| `FUN_140315400`, `FUN_140329cd0`, `FUN_14042dcd0` | TransformWidget, AlignmentWidget и DirectionWidget оригинального Layout; setupUi проверены 2026-10-03 | Форма, оригинальные PNG-состояния/@2x, групповое редактирование и Undo; точные визуальные границы ещё не восстановлены: [Layout](RE_wnd_Layout.md) |

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
| `AudioWaveformStyle` (integer) | `AudioWaveforms` (английский токен выбора) | Применяется в TimelineCanvas: RMS/Peak; native enum mapping не восстановлен |
| `WaveformScaleType` (integer) | `LogWaveform` (bool) | Применяется в waveform: логарифмическая/линейная шкала; тип отличается от оригинала |
| `CompositionLength` (строка) | `CompositeShotDefaultDuration` | Применяется при создании композиции; native формат строки отдельно не проверен |
| `EditorSequenceLength` (строка) | `EditorDefaultDuration` | ProjectDefaults задаёт frameCount/outPoint новой EditorSequence; отдельный native editor timeline не реализован |
| `SequenceObjectDefaultImageLength` | `PlaneDefaultDuration` | У нас применяется к Plane; не доказана эквивалентность для всех native image assets |
| `SavePathsAsRelative` | `UseRelativePaths` | Рабочее расширение сериализатора OpenVegas |
| `AutoSleepWakeAssets` | `CloseMediaOnInactive` | Освобождаются ресурсы MainWindow при неактивности; полный lifecycle оригинала не восстановлен |
| `AnalyticsReportingEnabled` | `Analytics` | Только хранение; `Debug/PrintAnalytics` — отдельный локальный журнал |
| `ShowProjectSettingsDialog` | `Prompts/ShowProjectSettings` | New Project читает флаг и открывает свойства при включении |
| `ProxyMediaQuality` (integer) | `ProxyQuality` (текст выбора) | Задаёт CRF/global_quality при английских токенах Low/Medium/High; Options сохраняет переведённый currentText, поэтому локализованные Low/High дают fallback Medium. Native enum mapping не установлен |
| `ProxyPreferIntegratedGPU` | `PreferIntegratedGPU` | Выбирает h264_qsv после пробного кодирования; fallback libx264; не общий выбор GPU рендера |
| `EnableEffectPresetCreation` | `Debug/EffectPresetCreation` | Только хранение; наш ключ находится в группе Debug |
| `ShowHardwareDecodingIndicator` | `Debug/HardwareDecodingIndicator` | Только хранение; достоверного индикатора активного декодера нет |
| `TimelineCacheDB`, `TimelineCacheFiles`, `DaysToKeepTimelineCacheFiles` | `TimelineCache`, `TimelineCacheDays` | Неполное сопоставление: у нас нет отдельной пары database/files с native схемой |

Точные строки `DefaultTemplate`, `EditorDefaultDuration`, `RenderThreads`,
`LogWaveform` в этой выборке Ghidra не найдены. Отрицательный string search не
доказывает отсутствия соответствующей функции в оригинале. Default Template у нас
использует `app/AVTemplates`: 81 встроенный формат и пользовательские `.hft`,
стабильные ID, Save/Delete в CompositionSettingsDialog. Имена встроенных форматов
взяты из каталогов референса; параметры заданы портом, установочные `.hft` оригинала
не получены. Формат пользовательских `.hft` исследован отдельно; отсутствие строки
в прежней выборке не означает отсутствие реализованного потребителя.

## 2. Исправленные заглушки

| Элемент/ключ | Реальное действие и место реализации |
|---|---|
| Record Voiceover | `VoiceoverDialog.cpp`: запись микрофона, обратный отсчёт, остановка, сохранение WAV, отмена; `MainWindow.cpp`: добавление отдельного слоя в позицию начала записи с Undo/Redo |
| `Options/Voiceover/Device` | `media::audioInputDevices`: DirectShow на Windows, CoreAudio на macOS, pactl/PulseAudio на Linux; ID dshow:/qtsound:/pulse:. AudioCapture проверяет выбранный ID; default оставляет выбор libVLC/ОС. Старые Qt ID требуют повторного выбора |
| `Options/Voiceover/Channels`, `/SampleRate` | Проверяется PcmFormat, libVLC callbacks запрашивают S16N с заданными каналами/частотой, WAV использует тот же формат. Нативный формат микрофона и аппаратная поддержка комбинации не перечисляются: VLC может конвертировать входной поток |
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
| `Options/UseRelativePaths` | `ProjectSaveOptions`, обычное сохранение: Filename, пути моделей и MediaID/ModelAssetID в расширениях сохраняются относительно проекта; при загрузке разрешаются относительно его каталога. Автосохранения лежат в своей папке и всегда пишут абсолютные пути |
| `Options/IncludeScreenLayout` | Сохраняет Qt dock-state как base64 в `OpenVegasScreenLayout`; при открытии применяется с проверкой версии Qt state и восстановлением нижних dock-углов/меню |
| `Options/Labels/%1/Name`, `/Color` | Меню меток таймлайна предлагает восемь сохранённых имён/цветов и отдельный произвольный цвет; изменение использует общий Undo/Redo; заблокированный слой не редактируется |
| `Options/RemoveExtensions` | ExportPanel использует имя композиции; удаляет известное исходное расширение из основы имени, сохраняя расширение выбранного формата экспорта |
| Export Frame при пресете MP4/MOV | Один кадр экспортируется в PNG; Export Contents продолжает использовать выбранный видеоформат. Ранее image.save пытался записать кадр как MP4 |
| `Debug/PrintAnalytics` | Включает локальные записи `UI event` для зарегистрированных QAction; внешняя телеметрия не отправляется |
| Browse Tutorials в Learn | Вызывает рабочую команду Online Help вместо сообщения об отсутствии встроенных уроков |
| Timeline `renderRequested`/`optionsRequested` | Удалены неиспользуемые сигналы и соединение. Рендер по scrub/изменению модели и команда Render Frame остаются рабочими |
| Viewer `m_texts`, `m_textDraft`, `m_textPlaceView` | Удалён мёртвый рисующий путь. Текст создаётся и редактируется через реальные слои композиции и TextRender |
| Viewer: оверлеи, рамка текста, 360, custom UI | `ViewerOverlay` (рисование и события раньше инструмента), `TextTransformOverlay` (перемещение/масштаб/поворот текста с Undo и ключами), 360-режим того же вьюера во вкладке «360 Viewer», `NativeCustomUiOverlay` для custom UI native-модулей (`Notify 1001..1013`) |
| Строка слоя таймлайна (`LayerPropertyTreeLayerWidget`) | Замок (`unlock`/`lock`), глаз (`video-on-checked`/`video-off`), метка, InLineEdit «N. имя [Тип]» (двойной щелчок — переименование «Set Layer Name», Esc — отмена), Motion Blur (`motion-blur[-checked]`, «Set Layer Motion Blur»), 2D/3D (`two-d`/`three-d`), Parent («None» + слои без циклов, «Set Layer Parent(s)»). Blend остался в панели Layer. Иконки трассированы из ресурсов референса в `resources/icons/timeline` |
| Motion Blur слоя и композиции | Переключатель в строке слоя (`MotionBlurOn`) + `RenderSettings` композиции (CompositionSettingsDialog `FUN_140730a50`: Enable/Shutter Angle/Shutter Phase/Max Samples/Use Adaptive) в «Composite Shot Properties»; CPU-рендер усредняет под-кадры затвора для анимированного 2D-трансформа |
| Fog композиции | Группа Fog того же диалога (`FUN_14072b930`, тексты `FUN_140730a50`): Enable, Near/Far Clip Distance (до 999999999), Density, Fall Off (Linear / Exponential / Exponential²), Color; формула шейдера Flux `ComputeFoggedFragment` (`model3d::Fog`): туман по расстоянию от камеры для 3D-слоёв, 3D-текста с геометрией и моделей; 2D-слои не затрагивает. Для этого медиа/Plane/текст/вложенные композиты в 3D теперь проецируются через камеру сцены (раньше рисовались плоско) |
| Группы слоя (AssetLayerGroupFactory, `FUN_1405ce1e0`) | Tracks/Masks/Transform/Behaviors — только для ассета с картинкой; Masks без «+»; Audio › Level (`FUN_1405e7bd0`: `audioLevel`, dB, FloatEditor) — для ассета со звуком; у mp3 только Effects и Audio. Level анимируется как свойства Transform (`TransformProperty::AudioLevel`) |

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

- Media › Properties (`biff::ui::media::MediaSettingsDialog`, 3 октября 2026): имя, путь с
  Relink, контейнер, длительность, разрешение и все переопределения референса — частота кадров
  и альфа с «From File», Aspect Ratio, Color Levels, Color Space, аппаратное декодирование, с
  шагами History. Дополнен Format (FourCC)/Codec (описание) и аудиоблоком
  Sample Rate/Channels/Audio Stream: адаптеры VLC 3/4 копируют сведения до освобождения
  track list. Audio-only файлы тоже имеют Properties, без видеопереопределений.
  Выбранный аудиопоток применяется до декодирования Play/Scrub, перед обработкой Audio
  .hfpl в экспорте, в прямом FFmpeg-экспорте и waveform; ключ waveform включает поток.
  Undo/Redo, Relink и offline save/load сохраняют аудиоиндекс в OpenVegasAudioStream;
  соответствие native AudioIdx не заявляется. Automatic на многодорожечном файле
  не показывает частоту/кодек произвольного потока. Интеграционные тесты проверяют
  две дорожки 32/44.1 kHz, PCM мастера, обе ветки экспорта (включая готовый MOV),
  Undo и повторный load/save с исходным XML. VLC 4 и другие ОС пока проверены только
  по заголовкам/сборке, без runtime-теста.
- AudioPlayer использует libVLC для декодирования и системного вывода PCM.
  Активные клипы нескольких видимых, незаглушённых слоёв суммируются с учётом
  sourceStart, speed, gain и вложенных композиций. Audio Meters получает RMS
  того же ограниченного итогового PCM после master mute. Очереди декодеров
  ограничены, stop/seek отменяют ожидания и устаревшие queued-сигналы.
  Нативные Audio `.hfpl` теперь действуют на PCM клипа до gain и master mix,
  параметры `.hfpl` пересчитываются на каждом 10-мс блоке по keyframes.
  AudioTransition подключён в realtime и экспорте; AudioExport обрабатывает
  анимированные native-параметры блоками по 480 frames при 48 kHz.
  Realtime speed меняет высоту тона через ресемплирование; export использует FFmpeg atempo.
  Наличие Audio DSP не означает точный realtime-результат для любого GetSampleRanges,
  цепочки и длины клипа (границы перечислены ниже).
- Маски и Behaviors в Controls используют общие редакторы и Undo/Redo. Разделение
  UI-параметров Behaviors не означает завершённое исполнение закрытого ABI всех `.hfpl`.
- Text: реальные слои, форматирование, цвета, Point/Paragraph, редактирование на канвасе.
- Vector Path создаёт Freehand mask/path через рабочий путь таймлайна.
- Waveform: `media/AudioWaveform` декодирует PCM через FFmpeg, хранит peaks/RMS в RAM
  и `.peaks` на диске; TimelineCanvas применяет стиль/шкалу и slip/speed.
  Ключ включает аудиопоток; native формат peak-файла не восстановлен.
- Proxy: Media › Proxy (None/Performance/Quality), очередь ProxyGenerator, каталоги
  ProxyDirectoryPath, PreviewMode Auto/Proxy/Full Resolution; экспорт читает оригиналы.
  Настройки ProxyQuality/PreviewMode пока сохраняют переведённый текст вместо стабильных
  токенов: backend сравнивает английские строки; локализованный режим может отключить proxy.
  FFmpeg пишет `.part`, после успеха файл переименовывается. Замена существующего
  файла через remove/rename не является гарантированно атомарной при ошибке rename.
- Timeline cache: FrameDiskCache сохраняет playback frames, читается до рендера
  в другом RenderManager, включает вложенные композиции/медиа в ключ; TimelineCacheDays
  и автозапуск от плейхеда через UseAutomaticRenderCache/RenderCacheDelay подключены.
- ExportQueue/ExportQueueView/ExportJob: снимки проекта, последовательные задачи,
  start/suspend, duplicate/remove/reveal, сохранение ExportTasks.xml; TimeFormat
  форматирует Duration/Elapsed (Timecode / Natural / Seconds). Вкладки Presets нет.
- Сохранение проекта: VegfxSerializer накладывает модель на исходный документ
  через VegfxMerge, сохраняя неизвестные поля существующих элементов. Это не полный
  парсер native схемы и не гарантия неизменности любой неизвестной структуры.
- Layout: восстановлены форма и иконки, опорная точка, выравнивание/распределение,
  отражение и поворот незаблокированной выборки одним Undo по ID, сохранение ключей.
  Геометрические ограничения отдельно описаны в [RE_wnd_Layout](RE_wnd_Layout.md).
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
| Audio preview / Audio Meters | libVLC PCM master mix активных Audio/Video клипов: visible/muted, offset, speed, gain, анимируемый Audio Level и вложенные композиции; native Audio `.hfpl`, AudioTransition, seek/loop, master RMS после mute | Будущее сухого сигнала доступно только первому эффекту на 0,25 с; дальше окна и у последующих эффектов — тишина. Короткий Reverse и окна NoiseReduction могут работать, произвольный Reverse/цепочка не гарантированы. Сохранение pitch при realtime speed не реализовано |
| Half/Quarter preview | RenderWorker рендерит в displaySize() композиции, затем уменьшает готовый кадр; кадрирование и PAR сохраняются | Единый уменьшенный масштаб слоёв/текста/3D/вложенных композитов/эффектов с пересчётом пиксельных радиусов; сейчас эффекты не обрабатываются быстрее Full |
| Видеоэкспорт со звуком | MainWindow и ExportQueue запускают `render/ExportJob`; `AudioExport::buildExportAudioInputs` готовит прямые или обработанные PCM-источники, native Audio/AudioTransition/анимируемый Level, затем FFmpeg atempo/volume/adelay/amix | Отдельный offline-путь, не использует AudioPlayer/master mute и его meter. Обработанный PCM фиксирован в 48 kHz; частота конечного файла определяется FFmpeg/входами, явный output -ar не задаётся. AudioSampleRate композиции хранится и показывается, но не выбирает частоту этого конвейера |
| Media Cache DB | AppMain::initializeCache: каталог, SQLite open, подсчёт записей, pruneOlderThan | Нет вызовов CacheDB::put/get в медиапайплайне. RAM video-frame cache MediaManager ограничен 256 MiB; waveform/timeline/proxy имеют отдельные хранилища и не доказывают наполнение Media Cache DB |
| Thumbnail cache limit | ThumbnailCacheSizeMB задаёт лимит QPixmapCache; Media повторно использует миниатюры по path/mtime/size, QImageReader декодирует уменьшенное изображение | QIcon видимого списка держит собственные ссылки; это не лимит всей памяти Media |
| Pre-render составного кадра | Make/Remove Pre-Render(s) в меню клипа: кадры композиции поверх прозрачности в PreRenderDirectoryPath, читаются вместо живого рендера при совпадении ключа состояния | Нет полноценной очереди задач pre-render и автоматического обновления изменённых кадров; исходный native cache format не восстановлен |
| Layout | Числовые поля/опорная точка, link, alignment/distribution; групповые mirror/rotate, Undo по ID, правка анимации в текущем кадре | MainWindow::layerBounds() использует размер композиции и локальные Position/Scale. Не учитывает размер медиа/текста, Rotation/Anchor/Parent, 3D-проекцию и границы эффектов; native numeric drag/scrub также не перенесён |
| Proxy options и публикация | Генерация FFmpeg и выбор preview подключены | OptionsDialog сохраняет ProxyQuality/PreviewMode через переведённый currentText, а ProxyMedia/updateProxyPreview сравнивают английские токены: нужны стабильные значения и миграция. Remove/rename существующего файла также не гарантирует атомарную замену |
| 3D project settings | ProjectSettingsDialog редактирует BPC 1000..1002, AntialiasingMode 1..9, карты, LimitVideoDecodingTo8bit, UseLinearColor; VegfxSerializer читает/пишет поля. ProjectDefaults читает соответствующие ключи Options | Renderer не применяет битность/MSAA/размеры карт/linear color. Дополнительно Options/Antialiasing записывается текстом, а ProjectDefaults ожидает integer 1..9: новый проект получает fallback 1; нужно согласовать тип и миграцию |
| Автосохранение и восстановление | ui/AutoSave: папка и период, `<проект>.vegfx.autosave<N>`, маркер исходного проекта, только при изменениях, очистка при ручном сохранении; lock-файл и Recovered Projects Open/Save/Delete | Старые `<проект>.autosave.vegfx` рядом с проектом удаляются при сохранении, но в списке восстановления не показываются |

## 4. Оставшиеся заглушки — пока не закрыты

Layer/360 Viewer дополнительно исследованы в Ghidra и реализованы:
[Layer](RE_wnd_Layer.md), [360 Viewer](RE_wnd_Viewer360.md). Layer получил
отдельный preview выделенного слоя; 360 — исправленные повороты, Roll,
рабочий camera FOV и отмену Properties без изменения состояния.
Wrap No/Tile/Reflect восстановлены через Notify(2) EnvironmentMapViewer.hfpl.
Native render pipeline, lens model и Fisheye/Scale/Motion Blur пока не восстановлены полностью.
Layout также имеет открытый геометрический путь: [аудит Раскладки](RE_wnd_Layout.md).

| Группа | Настройки/элементы без полного потребителя | Что требуется |
|---|---|---|
| Analytics | `Options/Analytics` | Отдельная политика аналитики; Debug/PrintAnalytics реализует только локальное журналирование действий |
| Prompts & Warnings | `Options/Prompts/GPUDriverWarning`, `QuickTimeWarning` | Нет самих функций (QuickTime, переносимая проверка версии драйвера); подключены RemovingExportTasks (удаление задач очереди экспорта), GPUWarning, MediaMismatchPrompt, OversizedAssets, ImageSequenceImportPrompt, ShowProjectSettings и правило камеры `Adding3DCameras`/`Removing3DCameras` ([RE_3D_Camera_Rule](RE_3D_Camera_Rule.md)) |
| Render | `TurboRendering`, планировщик `RenderThreads`, `LimitVideoDecodingTo8bit`, уменьшенный масштаб Half/Quarter | UseHardwareDecoding и avcodec-threads передаются VLC; UseHardwareEncoding выбирает рабочий аппаратный H.264 (NVENC/QSV/AMF/MF) для экспорта MP4. Конвейер порта целиком 8-битный, поэтому LimitVideoDecodingTo8bit ничего не меняет; Turbo и потоки рендера не подключены |
| Media cache | `MediaCacheDB`, `MediaCacheFiles`, `DaysToKeepMediaCacheFiles` | Startup open/prune работает; требуется наполнение и повторное использование кеша медиапайплайном |
| 3D Render/Display | `ModelTextureMaxSize`, `ShadowMapSize`, `ReflectionMapSize`, `Antialiasing`, `ShowCheckerboard3D`, `ShowFloorPlane` | Соответствующие рендер-пути и текстуры. 3D-слои видны в перспективе камеры с туманом и складываются по глубине внутри сцены (2D-слой её разрывает); режимы наложения в сцене, маски и рамки выделения вьюера остаются 2D-приближением |
| Proxy options | ProxyQuality/PreviewMode и безопасная замена файла | Сохранять стабильные токены, читать старые переводы; тесты ru/ja/zh_CN и смены языка. Ошибка публикации нового proxy не должна удалять существующий |
| Export | Пресеты экспорта | `TimeFormat` закрыт: Timecode / Natural / Seconds форматируют Duration и Elapsed очереди экспорта. Нет вкладки Presets референса (ExportPresetManager: встроенные и пользовательские пресеты, редактор свойств) — панель предлагает пять форматов |
| Audio format | AudioSampleRate композиции | Подключить значение к realtime/offline PCM и выходному кодированию; realtime и обработанный offline PCM работают в 48 kHz; FFmpeg выбирает частоту итогового файла, поле в Properties не управляет этим выбором |
| Layout | Точные visual bounds; native drag/scrub числовых полей | Собственный размер слоя, Rotation/Anchor/Parent и 3D-проекция; критерий — alignment соответствует видимому кадру, а не условной рамке композиции |
| Debug | `HardwareDecodingIndicator`, `EffectPresetCreation` | Достоверный статус используемого декодера и отдельный debug-путь создания presets; обычные пользовательские presets уже работают |
| Models | Строки `Models`/nodeNames | Сохранение связи треугольников с узлами, затем per-node visibility/transform/selection; сейчас mesh плоский и строка выбирает весь слой |

Перечисленные опции могут сохраняться в INI, но это не делает их реализованными.
Чеклист верхнего уровня остаётся незавершённым. Само наличие строки или widget-name в
Ghidra не является достаточным основанием считать backend восстановленным.

### 4.1 Критерии закрытия открытых пунктов

| Приоритет | Пункт | Наблюдаемая проверка |
|---|---|---|
| 1 | Полный realtime native Audio | Длинный Reverse, запросы будущего за 0,25 с и у второго/следующих эффектов возвращают нужный PCM; seek/loop не ломают историю, очереди/память ограничены. Прохождение короткого Reverse не закрывает весь путь |
| 1 | Layout visual bounds | Разные размеры медиа/текста, поворот, anchor/parent и 3D; выравнивание/размеры совпадают с Viewer, групповой Undo и save/load сохраняются |
| 2 | Media Cache DB | Импорт/декодирование наполняют кеш, повторное открытие реально читает его; изменение файла инвалидирует записи, TTL удаляет файлы и записи |
| 2 | Render/GPU/3D и Half/Quarter | Реальный backend/выход меняются при настройке, fallback проверен; тип Options/Antialiasing согласован. Уменьшение размера сокращает работу по всему конвейеру, без изменения кадрирования и радиусов эффектов |
| 2 | AudioSampleRate | Изменение частоты композиции меняет PCM и конечный файл, сохраняется после load/save; метаданные файла проверяются отдельно от UI |
| 2 | Proxy settings и замена | Low/High и Auto/Proxy работают после сохранения Options на ru/ja/zh_CN и переключения языка; ошибка rename сохраняет предыдущий proxy |
| 2 | Очередь pre-render | Очередь, отмена, прогресс, повторный запуск и инвалидирование изменённых композиций; обычный playback cache не закрывает этот пункт |
| 2 | Оставшиеся prompts | Есть сама проверка драйвера/QuickTime и подтверждённый сценарий сообщения; сохранения флажка недостаточно |
| 3 | Export presets | Built-in/user presets, редактор, save/delete и назначение задаче; TimeFormat уже работает и не относится к этому остатку |
| 3 | Models | Выбор конкретного node, visibility/transform, Undo и save/load; выбор всего mesh вместо узла не закрывает пункт |

Waveform, timeline cache, proxy generation, ограничение QPixmapCache и export TimeFormat
перенесены в реализованные/частичные пути выше. Native enum/форматы, общий лимит памяти
и аппаратные проверки остаются отдельными ограничениями.

Новые ограничения этой редакции подтверждены следующими цепочками кода:

| Путь | Запись/интерфейс | Потребитель и ограничение |
|---|---|---|
| Antialiasing | [OptionsDialog](../src/ui/OptionsDialog.cpp): saveSettings пишет currentText | [ProjectDefaults](../src/app/ProjectDefaults.h): toInt и enum 1..9; текст становится fallback 1 |
| ProxyQuality/PreviewMode | [OptionsDialog](../src/ui/OptionsDialog.cpp): переведённые элементы и currentText | [ProxyMedia](../src/media/ProxyMedia.cpp) сравнивает Low/High; [MainWindow](../src/ui/MainWindow.cpp)::updateProxyPreview сравнивает Auto/Proxy |
| AudioSampleRate | [CompositionSettingsDialog](../src/ui/CompositionSettingsDialog.cpp) и сериализатор сохраняют значение | [AudioPlayer](../src/media/AudioPlayer.cpp): Rate=48000; [AudioExport](../src/render/AudioExport.cpp): kRate=48000; [ExportJob](../src/render/ExportJob.cpp) не устанавливает частоту выходного файла из композиции |
| Публикация proxy | [ProxyMedia](../src/media/ProxyMedia.cpp)::ProxyGenerator::startNext пишет .part | После успеха remove(output), затем rename(part, output); при ошибке rename прежний output уже удалён |

### 4.2 Предыдущая live-проверка Ghidra и границы миграции

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

### 4.3 Закрытые потребители с учётом изменений до 2026-10-03

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
- DefaultTemplate: AVTemplates обслуживает 81 встроенный формат и пользовательские
  `.hft`; size/fps/PAR/audio sample rate выбираются по DefaultTemplateId.
  Прежние fullhd30/fullhd60/uhd30 остаются совместимыми ID; старое имя читается
  при миграции. Смена языка не сбрасывает ID. Наличие поля sample rate не означает
  применение этой частоты в текущем PCM-конвейере.
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
  VegfxMerge сохраняет неизвестные поля исходных элементов; это защита от потери
  данных при обычном round-trip, а не полный перенос всех native типов и алгоритмов.

## 5. Проверки

### 5.1 Проверки этой редакции (2026-10-04)

- Сверены реальные потребители waveform/proxy/template/editor duration, запись WAV,
  realtime/offline audio, Media Cache DB, render options, экспорт и Layout.
  Новых live-декомпилятов Ghidra, сборки и CTest в этой редакции не запускалось:
  изменён документ, код приложения не менялся.
- `python tools/validate_translations.py`: ru/ja/zh_CN — по 1404 актуальных сообщения,
  по 0 ошибок. Это структурная проверка TS, а не полнота покрытия новых строк кода.
- Проверены относительные ссылки и наличие названных методов/файлов; удалённая
  `MainWindow::finishVideoExport` заменена актуальным ExportJob/AudioExport.
- Выявлены дополнительные открытые пути: текстовый Options/Antialiasing против
  integer в ProjectDefaults; фиксированные 48 kHz realtime/обработанного PCM при редактируемом
  AudioSampleRate; переведённые ProxyQuality/PreviewMode вместо токенов;
  отсутствие гарантии атомарной замены существующего proxy файла.

### 5.2 Результаты предыдущих этапов

Следующие результаты относятся к указанным этапам и не являются новым полным прогоном.

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
  на предыдущем этапе проверено по 1001 сообщению без пустых/unfinished; актуальные
  количества и результат проверки этой редакции указаны в 5.1.
- Для актуализации аудита проверены string/xref/decompile связи, реальные потребители
  ключей и Settings getter, отсутствие обращения `CacheDB::put/get` вне реализации CacheDB,
  master mixer AudioPlayer и отдельный offline-путь AudioExport/ExportJob с `amix`.

- Этап 2026-10-03: CMake/MSVC Debug пересобран после исправления зависимостей Ninja;
  открытие `project_1/Project.vegfx` под CDB без прежнего MediaAsset access violation.
  53 Debug-проверки Text/Media прошли; три Layout-регрессии прошли отдельно,
  снимки формы проверены (подробности в [RE_wnd_Layout](RE_wnd_Layout.md)).
- Наличие регрессий для новых путей проверено по исходникам: `nativeEffectsSeeLookAheadWhilePlaying`,
  `transitionsCombineClipsInRealtime`, `waveformPeaksStylesAndCache`,
  `proxyMediaGenerationAndRoundTrip`, `timelineDiskCacheSurvivesManagersAndExpires`,
  `preRenderedShotIsReadInsteadOfRendered`, `exportQueueRunsTasksFromSnapshots`,
  `compositionTemplatesAndFormats`, `mediaAudioStreamsMetadataSelectionAndRoundTrip`.
  В этом аудите эти тесты повторно не выполнялись.

Offscreen-тесты не проверяют настоящий микрофон, аудиоустройство, системный native
color dialog или взаимодействие с оконным менеджером при смене активного приложения.
