#pragma once

#include "app/AVTemplates.h"
#include "app/Settings.h"
#include "composition/Composition.h"
#include <QtMath>

namespace openvegas::app {

// IDs remain stable when the UI language changes. Older builds stored display text.
inline QString defaultProjectTemplateId(const QSettings& settings)
{
    const QString id = settings.value(QStringLiteral("Options/DefaultTemplateId")).toString();
    if (!id.isEmpty() && !templateById(id).id.isEmpty()) return id;
    const QString legacy = settings.value(QStringLiteral("Options/DefaultTemplate")).toString();
    if (legacy.contains(QLatin1String("4K"), Qt::CaseInsensitive) || legacy.contains(QLatin1String("2160"))) return QStringLiteral("uhd30");
    if (legacy.contains(QLatin1String("1080")) && legacy.contains(QLatin1String("60"))) return QStringLiteral("fullhd60");
    return QStringLiteral("fullhd30");
}

// The Rendering defaults a new project takes, from Options > Render with the
// reference's own fallbacks: Options/RenderBitDepth (1000..1002, else 1000,
// FUN_14022e2a0), Options/Antialiasing (1..9, else 1, FUN_14022e500),
// Options/UseLinearColor and the three map sizes.
inline composition::ProjectRenderSettings defaultProjectRenderSettings()
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             Settings::organizationName(), Settings::applicationName());
    composition::ProjectRenderSettings s;
    const int bitDepth = settings.value(QStringLiteral("Options/RenderBitDepth"), 1000).toInt();
    s.bitDepth = bitDepth >= 1000 && bitDepth <= 1002 ? bitDepth : 1000;
    const int antialiasing = settings.value(QStringLiteral("Options/Antialiasing"), 1).toInt();
    s.antialiasingMode = antialiasing >= 1 && antialiasing <= 9 ? antialiasing : 1;
    s.useLinearColor = settings.value(QStringLiteral("Options/UseLinearColor"), false).toBool();
    s.reflectionMapSize = qMax(1, settings.value(QStringLiteral("Options/ReflectionMapSize"), 512).toInt());
    s.shadowMapSize = qMax(1, settings.value(QStringLiteral("Options/ShadowMapSize"), 2048).toInt());
    s.modelTextureMaxSize = qMax(1, settings.value(QStringLiteral("Options/ModelTextureMaxSize"), 4096).toInt());
    return s;
}

inline void applyNewProjectDefaults(composition::Composition& scene)
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             Settings::organizationName(), Settings::applicationName());
    // The Default Template's format (app/AVTemplates).
    const AVTemplate format = templateById(defaultProjectTemplateId(settings));
    int numerator = 30, denominator = 1;
    composition::Composition::frameRateFraction(format.frameRate, &numerator, &denominator);
    const double fps = double(numerator) / denominator;
    scene.setSize(format.width, format.height);
    scene.setFrameRate(numerator, denominator);
    scene.setPixelAspect(format.pixelAspect, format.pixelAspectValue);
    scene.setAudioSampleRate(format.audioSampleRate);
    scene.setDurationSeconds(Settings::compositeShotDefaultDurationSeconds());
    auto& sequence = scene.editorSequence();
    sequence = composition::EditorSequence();
    sequence.width = scene.width(); sequence.height = scene.height();
    sequence.fps = fps;
    sequence.frameCount = qMax<qint64>(1, qRound64(Settings::editorDefaultDurationSeconds() * fps));
    sequence.outPoint = sequence.frameCount;
    scene.projectSettings() = defaultProjectRenderSettings();
    // A new project's first shot is the one VEGAS Pro gets back (<IsPrimary>).
    scene.setPrimary(true);
}

} // namespace openvegas::app
