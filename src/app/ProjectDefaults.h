#pragma once

#include "app/Settings.h"
#include "composition/Composition.h"
#include <QtMath>

namespace openvegas::app {

// IDs remain stable when the UI language changes. Older builds stored display text.
inline QString defaultProjectTemplateId(const QSettings& settings)
{
    const QString id = settings.value(QStringLiteral("Options/DefaultTemplateId")).toString();
    if (id == QLatin1String("fullhd30") || id == QLatin1String("fullhd60") || id == QLatin1String("uhd30")) return id;
    const QString legacy = settings.value(QStringLiteral("Options/DefaultTemplate")).toString();
    if (legacy.contains(QLatin1String("4K"), Qt::CaseInsensitive) || legacy.contains(QLatin1String("2160"))) return QStringLiteral("uhd30");
    if (legacy.contains(QLatin1String("1080")) && legacy.contains(QLatin1String("60"))) return QStringLiteral("fullhd60");
    return QStringLiteral("fullhd30");
}

inline void applyNewProjectDefaults(composition::Composition& scene)
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             Settings::organizationName(), Settings::applicationName());
    const QString id = defaultProjectTemplateId(settings);
    const int fps = id == QLatin1String("fullhd60") ? 60 : 30;
    const bool uhd = id == QLatin1String("uhd30");
    scene.setSize(uhd ? 3840 : 1920, uhd ? 2160 : 1080);
    scene.setFrameRate(fps, 1);
    scene.setDurationSeconds(Settings::compositeShotDefaultDurationSeconds());
    auto& sequence = scene.editorSequence();
    sequence = composition::EditorSequence();
    sequence.width = scene.width(); sequence.height = scene.height();
    sequence.fps = fps;
    sequence.frameCount = qMax<qint64>(1, qRound64(Settings::editorDefaultDurationSeconds() * fps));
    sequence.outPoint = sequence.frameCount;
}

} // namespace openvegas::app
