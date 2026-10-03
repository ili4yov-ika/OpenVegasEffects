# 360 Viewer

Проверено 2026-09-27 через Ghidra MCP, VegasEffects.exe.

## Подтверждено в Ghidra

Конструктор `FUN_1402906d0` создаёт Preview360VideoPanelWidget (`1412d1180`),
comboBoxCurrentView, кнопку свойств с icon-only стилем и glWidget. Первоначальные
`<Front>` (`1412d1228`) / `<Back>` (`1412d121c`) — временные элементы формы.
`FUN_140290e50`, вызываемая `FUN_1402910f0`, заменяет их семью направлениями:
Front=1, Back=2, Left=3, Right=4, Top=5, Bottom=6, Custom=0.

`FUN_140291410` обновляет combo с блокировкой сигналов;
`FUN_140291510` применяет itemData через `FUN_140380d60`. Последняя задаёт
`envTrasnformXRot/YRot/ZRot` (опечатка принадлежит оригиналу): Front — нулевые
углы, Back — Y=180°, Left — Y=90°, Right — Y=-90°, Top/Bottom — X=±90°.
Custom сохраняет произвольное направление.

`FUN_140380680` создаёт EffectInstance (ID 0x76f), задаёт `useCameraFOV=false`
и **целочисленные** `envWrapX=0`, `envWrapY=0`. Camera/lens model не установлен.
Строки Full/Planar не обозначают направления
этой панели.

`FUN_140381170` открывает EffectInstanceSettingsDialog, затем вызывает
`FUN_140380080`, повторно определяя направление по X/Y/Z Rotation. Константы
`1413334c8`, `1413334f0`, `141333640` проверены в PE: +90°, 180°, -90°.
Повторный Notify(2) `EnvironmentMapViewer.hfpl` через `hfpl_runtime_probe`
подтвердил `envWrapX/Y` choices **No=0, Tile=1, Reflect=2**, FOV **1–179°**.
Плагин также объявляет useFishEyeLens, envTexScale, envScaleRatio и Motion Blur;
их полная реализация пока отсутствует. Совпадение имён параметров подтверждает
связь с панелью, но само по себе не доказывает идентичность всего render pipeline.

## Реализация

`Preview360.ui` подключена через setupUi. Семь направлений, кнопка Properties,
мышь, колесо, стрелки и Home работают. Стрелки поворачивают на 1°, Shift — 10°;
Home возвращает Front. Проекция equirectangular → perspective выполняется на CPU.
Повороты применяются в порядке Roll → Pitch → Yaw, поэтому изменение Yaw не
меняет широту выбранного направления. Bilinear sampling сохраняет alpha.

Properties редактирует Yaw, Pitch (включая ±90°), Roll, FOV, использование FOV
камеры и wrap по обеим осям. Cancel ничего не меняет; OK без изменений сохраняет
именованное направление. После OK точные preset-углы восстанавливают имя
направления. Combo обновляется без обратного вызова preset.
Настройки сохраняются в QSettings/Viewer360, включая Roll.

Use camera FOV использует вертикальный FOV первой видимой камеры композиции
(без камеры — 39.6°). Значение общее с 3D renderer и сохраняется как расширение
нашего проекта. Колесо при этом переключает просмотр на ручной FOV и действительно
масштабирует изображение. Поле ручного FOV (1–179°) отключено, пока используется камера.
Wrap по каждой оси — No (краевой пиксель), Tile (повтор), Reflect (зеркальный
повтор), включая bilinear sampling на границе. Старые bool-настройки мигрируют
в No/Tile; новые WrapXMode/WrapYMode сохраняют все три режима. Версия
RotationConventionVersion мигрирует прежний Custom Yaw, сохраняя направление
при переходе на знаки поворотов оригинала.

Ограничения: это CPU-проекция итогового кадра композиции, а не native EffectInstance
с оригинальным GPU renderer. Fisheye, Scale/Scale Ratio и Motion Blur плагина
ещё требуют реализации. Native focal length/FOV conversion требует
отдельного исследования и не заменяется нашим расширением XML.

## 360 Viewer — тот же Viewer (1 октября 2026)

Направление, объектив и wrap вынесены в `ui::Viewer360View` — общий для `Preview360VideoWidget`
и `ViewerWidget`. Вкладка «360 Viewer» сохраняет свою шапку (направление и Properties), а при
выборе принимает страницу главного вьюера (инструменты, транспорт, параметры вида), и вьюер
переходит в 360-режим: холст — перспективная проекция equirectangular-кадра, «Выделение» и
«Рука» поворачивают взгляд, колесо меняет FOV, стрелки поворачивают на 1° (Shift — 10°), Home
возвращает Front. Отображение холст ↔ вид проходит через объектив в обе стороны, поэтому
инструменты «Текст» и маски ставят точки в пиксели панорамы, а путь движения рисуется по
кривой проекции (за спиной — скрыт). При возврате на вкладку «Viewer» страница переносится
обратно; если обе вкладки видны (одна отстыкована), побеждает выбранная последней, во
второй — кнопка «Показать вьюер здесь». Решение принимается по сигналам `visibilityChanged`
после их завершения (`isVisible()` у неактивной вкладки остаётся true).

## Проверки

`timeline_regression`: viewer360ProjectionAndControls,
viewer360RotationAndPropertiesCancel, viewer360NativeWrapAndDirectionRecovery,
cameraFovProjectRoundTrip, viewer360ModeIsTheSameViewer.

## Live-проверка 2026-09-27

Повторный live-декомпилят FUN_140381170 подтверждает чтение envTrasnformXRot/YRot/ZRot после Properties, восстановление направления через FUN_140380080 и changed/update. GPU EffectInstance и lens model остаются неисследованными до уровня точного воспроизведения.
