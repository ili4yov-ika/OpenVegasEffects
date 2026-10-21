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
    // Optional stored values for dynamic controls (notably native LayerPicker).
    // Labels may be renamed or duplicated; layer IDs must survive either case.
    QStringList choiceValues;
    QString group;
    // Options bit 0x2 of the control (the Create* services' flags): the
    // property exists and is read, but the Controls and timeline trees leave
    // it out - Glow keeps an old integer "radius" next to "radius_f" that way
    // (VegasEffects FUN_14077ba20/FUN_14077be30 skip such nodes).
    bool hidden = false;
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

    // The Qt translation context of a native plugin's own strings,
    // "Effect_<ShortName>"; the names, labels, units, groups and combo items
    // above are already translated in it.
    QString translationContext;

    // A plugin's own GUID (reference PluginMetadata +0x04). Unique per plugin
    // across the whole shipped set, which is what makes it a dependable key
    // when two files declare the same "com.FXHOME.HitFilm.*" identifier.
    QString guid;

    // What the plugin says of itself (AbstractPlugin::Author/Copyright; a
    // native module's PluginInfo vendor and copyright), for About.
    QString author;
    QString copyright;

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
