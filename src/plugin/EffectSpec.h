#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/Version.h"
#include "plugin/PluginId.h"

namespace openvegas {
namespace plugin {

struct EffectParameterSpec
{
    QString name;
    QString displayName;
    QString type = QStringLiteral("string");
    QString defaultValue;
    // Shown after the value, as the reference does ("9.48 px", "100 %"). Its
    // plugins carry the unit as a separate translatable string.
    QString unit;
    // Native path editors carry the QFileDialog-style filter separately from
    // the stored property value (for example "Cube LUT (*.cube)").
    QString fileFilter;
    double minimum = -100000.0;
    double maximum = 100000.0;
    int decimals = 2;
    double step = 1.0;
    QStringList choices;
    QString group;
};

struct EffectSpec
{
    PluginId id;
    QString name;
    QString displayName;
    QString category;
    QString description;
    core::Version version;
    PluginKind kind = PluginKind::Effects;
    QVector<EffectParameterSpec> parameters;

    // Sub-category, when the plugin names one after a "|": the reference ships
    // "Keying|Matte Enhancement" and four flavours of "Transitions - Video|...".
    QString subCategory;

    // Search keywords the plugin carries in braces after its name -
    // "Chroma Key {greenscreen green screen bluescreen}". The Effects panel
    // matches on them as well as on the name.
    QStringList keywords;

    // A plugin's own GUID (reference PluginMetadata +0x04). Unique per plugin
    // across the whole shipped set, which is what makes it a dependable key
    // when two files declare the same "com.FXHOME.HitFilm.*" identifier.
    QString guid;

    // False when the effect can be listed but not run. The recovered native
    // host currently covers 2D, video-transition and audio ABIs; unsupported
    // Geometry/Behavior modules remain visible with an explicit reason.
    bool renderable = true;

    // Why it cannot render, for the message the window shows.
    QString unavailableReason;

    QString kindName() const;
};

} // namespace plugin
} // namespace openvegas
