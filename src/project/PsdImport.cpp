#include "project/PsdImport.h"

#include "media/MediaManager.h"
#include "media/PsdDocument.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLineF>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QStandardPaths>
#include <QTransform>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

// How a Photoshop document becomes a composite shot. Layer records are read
// bottom to top and the composition lists its layers top first (index 0 is
// drawn last), so the layers are reversed at the end. Each layer's pixels are a media item of their own, named by a
// marked path (media/PsdDocument), so a project keeps pointing at the .psd
// and shows its layers as they are when it is opened again.
//
// The effects are the reference's own plug-ins (Drop Shadow, Outer Glow,
// Exposure Pro, ...); their parameters are set by name, so a missing plug-in
// only drops that effect, with a note.

namespace openvegas {
namespace project {

namespace {

constexpr double kPi = 3.14159265358979323846;

// The notes' and effect names' translation context.
class PsdImportText
{
    Q_DECLARE_TR_FUNCTIONS(openvegas::project::PsdImport)
};

// A text marked with QT_TRANSLATE_NOOP in that context, translated.
QString translated(const char* text)
{
    return QCoreApplication::translate("openvegas::project::PsdImport", text);
}

// The English names Photoshop shows its blend modes by, for the notes.
QString photoshopModeName(const QString& key)
{
    static const QHash<QString, const char*> names = {
        {QStringLiteral("pass"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Pass Through")},
        {QStringLiteral("diss"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Dissolve")},
        {QStringLiteral("lbrn"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Linear Burn")},
        {QStringLiteral("dkCl"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Darker Color")},
        {QStringLiteral("lgCl"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Lighter Color")},
        {QStringLiteral("vLit"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Vivid Light")},
        {QStringLiteral("lLit"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Linear Light")},
        {QStringLiteral("pLit"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Pin Light")},
        {QStringLiteral("hMix"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Hard Mix")},
        {QStringLiteral("fsub"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Subtract")},
        {QStringLiteral("fdiv"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Divide")},
        {QStringLiteral("hue "), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Hue")},
        {QStringLiteral("sat "), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Saturation")},
        {QStringLiteral("colr"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Color")},
        {QStringLiteral("lum "), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Luminosity")},
    };
    const auto it = names.constFind(key);
    return it == names.cend() ? key.trimmed() : translated(it.value());
}

void addNote(QStringList* notes, const QString& note)
{
    if (notes && !notes->contains(note)) notes->append(note);
}

QString number(double value)
{
    return QString::number(value, 'g', 8);
}

// A new instance of an installed plug-in with its defaults and the values
// given by parameter name; nothing (and a note) when it is not installed.
std::optional<composition::Effect> makeEffect(const EffectSpecLookup& specOf, const QString& identifier,
                                              const QString& name, const QHash<QString, QString>& values,
                                              QStringList* notes)
{
    const plugin::EffectSpec spec = specOf ? specOf(identifier) : plugin::EffectSpec();
    if (!spec.id.isValid() || (spec.parameters.isEmpty() && !values.isEmpty())) {
        addNote(notes, PsdImportText::tr("The \"%1\" effect plug-in is not installed; %2 is left out.")
                           .arg(identifier.section(QLatin1Char('.'), -1), name));
        return std::nullopt;
    }
    composition::Effect effect;
    effect.pluginId = spec.id;
    effect.name = name;
    for (const plugin::EffectParameterSpec& parameter : spec.parameters)
        effect.parameterValues.append(values.value(parameter.name, parameter.defaultValue));
    return effect;
}

bool flag(const QVariantMap& map, const char* key, bool fallback)
{
    const QVariant value = map.value(QLatin1String(key));
    return value.isValid() ? value.toBool() : fallback;
}

double valueOf(const QVariantMap& map, const char* key, double fallback = 0.0)
{
    return media::psdNumber(map.value(QLatin1String(key)), fallback);
}

// The instances of one kind of layer effect: a style holds either one
// ('DrSh') or, since Photoshop CC 2015, a list ('dropShadowMulti').
QVector<QVariantMap> effectInstances(const QVariantMap& style, const char* single, const char* multi)
{
    QVector<QVariantMap> out;
    if (style.contains(QLatin1String(multi))) {
        for (const QVariant& item : style.value(QLatin1String(multi)).toList()) out.append(item.toMap());
    } else if (style.contains(QLatin1String(single))) {
        out.append(style.value(QLatin1String(single)).toMap());
    }
    QVector<QVariantMap> enabled;
    for (const QVariantMap& effect : out)
        if (flag(effect, "enab", true) && flag(effect, "present", true)) enabled.append(effect);
    return enabled;
}

// Drop Shadow's angle for a Photoshop light angle. Photoshop's angle says
// where the light comes from, counter-clockwise from the right, and casts
// the shadow the other way; the plug-in's angle is the direction the shadow
// is cast, clockwise from straight up (180 below, 270 to the left - measured
// by the test psdDropShadowFallsAwayFromTheLight).
double dropShadowAngle(double photoshopAngle)
{
    return std::fmod(270.0 - photoshopAngle + 360.0 * 4, 360.0);
}

// Camera Raw's settings of a smart filter: its descriptor keys ('Ex12',
// 'Cr12', ...), or the crs: attributes of XMP it carries as text.
struct CameraRaw
{
    QHash<QString, double> values;
    bool has(const QString& key) const { return values.contains(key); }
    double value(const QString& key) const { return values.value(key); }
};

void collectXmp(const QVariant& value, CameraRaw* raw, int depth = 0)
{
    if (depth > 8) return;
    if (value.typeId() == QMetaType::QVariantMap) {
        for (const QVariant& item : value.toMap()) collectXmp(item, raw, depth + 1);
        return;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant& item : value.toList()) collectXmp(item, raw, depth + 1);
        return;
    }
    const QString text = value.typeId() == QMetaType::QByteArray ? QString::fromUtf8(value.toByteArray())
                                                                  : value.toString();
    if (!text.contains(QLatin1String("crs:"))) return;
    static const QRegularExpression attribute(
        QStringLiteral("crs:([A-Za-z0-9]+)\\s*=\\s*\"([+\\-]?[0-9.]+)\""));
    static const QRegularExpression element(
        QStringLiteral("<crs:([A-Za-z0-9]+)>([+\\-]?[0-9.]+)</crs:"));
    for (const QRegularExpression* pattern : {&attribute, &element}) {
        auto it = pattern->globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            raw->values.insert(match.captured(1), match.captured(2).toDouble());
        }
    }
}

CameraRaw cameraRawSettings(const QVariantMap& filter)
{
    CameraRaw raw;
    collectXmp(filter, &raw);
    // The descriptor's own keys, which Photoshop's scripting writes.
    static const QHash<QString, QString> keys = {
        {QStringLiteral("Ex12"), QStringLiteral("Exposure2012")}, {QStringLiteral("Cr12"), QStringLiteral("Contrast2012")},
        {QStringLiteral("Hi12"), QStringLiteral("Highlights2012")}, {QStringLiteral("Sh12"), QStringLiteral("Shadows2012")},
        {QStringLiteral("Wh12"), QStringLiteral("Whites2012")}, {QStringLiteral("Bk12"), QStringLiteral("Blacks2012")},
        {QStringLiteral("Cl12"), QStringLiteral("Clarity2012")}, {QStringLiteral("Vibr"), QStringLiteral("Vibrance")},
        {QStringLiteral("Strt"), QStringLiteral("Saturation")}, {QStringLiteral("Temp"), QStringLiteral("Temperature")},
        {QStringLiteral("Tint"), QStringLiteral("Tint")}, {QStringLiteral("Dhze"), QStringLiteral("Dehaze")},
        {QStringLiteral("Dhz "), QStringLiteral("Dehaze")}, {QStringLiteral("Txtr"), QStringLiteral("Texture")},
    };
    for (auto it = filter.cbegin(); it != filter.cend(); ++it) {
        const QString name = keys.value(it.key(), it.key());
        bool ok = false;
        const double v = it.value().toDouble(&ok);
        if (ok && it.value().typeId() != QMetaType::Bool) raw.values.insert(name, v);
        else if (it.value().typeId() == QMetaType::QVariantMap
                 && it.value().toMap().contains(QStringLiteral("value")))
            raw.values.insert(name, media::psdNumber(it.value()));
    }
    return raw;
}

// The Camera Raw settings of a filter that change the picture and that the
// port does not reproduce (it reproduces the Basic panel's tone controls,
// Vibrance, Saturation and Dehaze through Exposure Pro and Dehaze), by name.
// A setting at its neutral value does not count: a hue whose saturation is
// zero, curve splits without amounts, a straight tone curve.
QStringList cameraRawUnconverted(const QVariantMap& filter)
{
    static const QSet<QString> converted = {
        QStringLiteral("Ex12"), QStringLiteral("Cr12"), QStringLiteral("Hi12"), QStringLiteral("Sh12"),
        QStringLiteral("Wh12"), QStringLiteral("Bk12"), QStringLiteral("Vibr"), QStringLiteral("Strt"),
        QStringLiteral("Dhze"), QStringLiteral("Dhz "),
    };
    // Version and bookkeeping keys, and settings that only shape others.
    static const QSet<QString> ignored = {
        QStringLiteral("_class"), QStringLiteral("CrVe"), QStringLiteral("PrVN"), QStringLiteral("PrVe"),
        QStringLiteral("WBal"), QStringLiteral("filterID"), QStringLiteral("comp"), QStringLiteral("compInfo"),
        QStringLiteral("PC_1"), QStringLiteral("PC_2"), QStringLiteral("PC_3"),
        QStringLiteral("STSH"), QStringLiteral("STHH"), QStringLiteral("STB "),
        QStringLiteral("GRNS"), QStringLiteral("GRNF"),
        QStringLiteral("PCVM"), QStringLiteral("PCVF"), QStringLiteral("PCVR"), QStringLiteral("PCVS"),
        QStringLiteral("PCVH"),
    };
    // Settings by the panel they belong to, for the note.
    const auto panel = [](const QString& key) -> const char* {
        if (key == QLatin1String("Temp")) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Temperature");
        if (key == QLatin1String("Tint")) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Tint");
        if (key == QLatin1String("Cl12")) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Clarity");
        if (key == QLatin1String("CrTx") || key == QLatin1String("Txtr"))
            return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Texture");
        if (key.startsWith(QLatin1String("PC_"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Parametric Curve");
        if (key.startsWith(QLatin1String("Crv"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Point Curve");
        if (key.startsWith(QLatin1String("HA_")) || key.startsWith(QLatin1String("SA_"))
            || key.startsWith(QLatin1String("LA_")))
            return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "HSL");
        if (key.startsWith(QLatin1String("ST"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Split Toning");
        if (key.startsWith(QLatin1String("Cg"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Color Grading");
        if (key.startsWith(QLatin1String("GRN"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Grain");
        if (key.startsWith(QLatin1String("PCV"))) return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Vignette");
        if (key == QLatin1String("RHue") || key == QLatin1String("RSat") || key == QLatin1String("GHue")
            || key == QLatin1String("GSat") || key == QLatin1String("BHue") || key == QLatin1String("BSat")
            || key == QLatin1String("BlkB"))
            return QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Calibration");
        return nullptr;
    };
    QStringList names;
    const auto add = [&names](const QString& name) { if (!names.contains(name)) names.append(name); };
    for (auto it = filter.cbegin(); it != filter.cend(); ++it) {
        const QString& key = it.key();
        if (converted.contains(key) || ignored.contains(key)) continue;
        bool changes = false;
        if (it.value().typeId() == QMetaType::QVariantList) {
            // A point curve: (input, output) pairs; straight when every
            // point is on the diagonal.
            const QVariantList points = it.value().toList();
            for (int i = 0; i + 1 < points.size(); i += 2)
                if (points.at(i).toInt() != points.at(i + 1).toInt()) changes = true;
        } else if (key.startsWith(QLatin1String("Cg"))) {
            // Color Grading: a wheel's hue alone and the blending do nothing.
            changes = !key.endsWith(QLatin1Char('H')) && key != QLatin1String("CgBl")
                      && media::psdNumber(it.value()) != 0.0;
        } else if (it.value().typeId() == QMetaType::Bool || it.value().typeId() == QMetaType::QString
                   || (it.value().typeId() == QMetaType::QVariantMap
                       && !it.value().toMap().contains(QStringLiteral("value")))) {
            continue;
        } else {
            changes = media::psdNumber(it.value()) != 0.0;
        }
        if (!changes) continue;
        const char* name = panel(key);
        add(name ? translated(name) : key.trimmed());
    }
    return names;
}

bool isCameraRawFilter(const QVariantMap& item, const QVariantMap& filter)
{
    const QString kind = filter.value(QStringLiteral("_class")).toString();
    return kind.contains(QLatin1String("Camera Raw"), Qt::CaseInsensitive)
           || item.value(QStringLiteral("Nm  ")).toString().contains(QLatin1String("Camera Raw"), Qt::CaseInsensitive);
}

} // namespace

QString psdBlendModeName(const QString& key, bool* exact)
{
    struct Mode { const char* key; const char* name; bool exact; };
    static const Mode modes[] = {
        {"norm", "Normal", true},       {"pass", "Normal", true},        {"diss", "Normal", false},
        {"dark", "Darken", true},       {"mul ", "Multiply", true},      {"idiv", "Color Burn", true},
        {"lbrn", "Color Burn", false},  {"dkCl", "Darken", false},       {"lite", "Lighten", true},
        {"scrn", "Screen", true},       {"div ", "Color Dodge", true},   {"lddg", "Add", true},
        {"lgCl", "Lighten", false},     {"over", "Overlay", true},       {"sLit", "Soft Light", true},
        {"hLit", "Hard Light", true},   {"vLit", "Hard Light", false},   {"lLit", "Hard Light", false},
        {"pLit", "Hard Light", false},  {"hMix", "Hard Light", false},   {"diff", "Difference", true},
        {"smud", "Exclusion", true},    {"fsub", "Difference", false},   {"fdiv", "Color Dodge", false},
        {"hue ", "Normal", false},      {"sat ", "Normal", false},       {"colr", "Normal", false},
        {"lum ", "Normal", false},
    };
    for (const Mode& mode : modes) {
        if (key == QLatin1String(mode.key)) {
            if (exact) *exact = mode.exact;
            return QString::fromLatin1(mode.name);
        }
    }
    if (exact) *exact = false;
    return QStringLiteral("Normal");
}

QVector<composition::Effect> psdLayerStyleEffects(const QVariantMap& style, int globalLightAngle,
                                                  const QSizeF& layerSize, const EffectSpecLookup& specOf,
                                                  const QString& layerName, QStringList* notes)
{
    QVector<composition::Effect> effects;
    if (style.isEmpty() || !flag(style, "masterFXSwitch", true)) return effects;
    // The style's "Scale Effects", applied to every size in it.
    const double scale = valueOf(style, "Scl ", 100.0) / 100.0;
    const auto add = [&effects](std::optional<composition::Effect> effect) {
        if (effect) effects.append(*effect);
    };
    const auto opacity = [](const QVariantMap& effect) { return qBound(0.0, valueOf(effect, "Opct", 100.0), 100.0); };
    const auto colour = [](const QVariantMap& effect, const QColor& fallback) {
        return media::psdColor(effect.value(QStringLiteral("Clr ")), fallback).name();
    };
    const auto modeNote = [&](const QVariantMap& effect, const QString& effectName) {
        const QString mode = effect.value(QStringLiteral("Md  ")).toString();
        static const QStringList expected = {QStringLiteral("Nrml"), QStringLiteral("normal"),
                                             QStringLiteral("Mltp"), QStringLiteral("Scrn")};
        if (!mode.isEmpty() && !expected.contains(mode))
            addNote(notes, PsdImportText::tr("%1: the blend mode of its %2 is not kept.").arg(layerName, effectName));
    };

    // Photoshop draws the overlays on the layer's pixels first, then the
    // inner effects, then the stroke, then the outer glow and the shadow
    // behind; effects here run in their list's order.
    for (const QVariantMap& fill : effectInstances(style, "SoFi", "solidFillMulti")) {
        modeNote(fill, PsdImportText::tr("Color Overlay"));
        add(makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Fill"), PsdImportText::tr("Color Overlay"),
                       {{QStringLiteral("fillColor"), colour(fill, Qt::white)},
                        {QStringLiteral("blendAmount"), number(opacity(fill))}}, notes));
    }
    for (const QVariantMap& gradient : effectInstances(style, "GrFl", "gradientFillMulti")) {
        // The first and last colour stops, along the style's angle across the
        // layer (Y up, from the layer's centre).
        const QVariantList stops = gradient.value(QStringLiteral("Grad")).toMap().value(QStringLiteral("Clrs")).toList();
        QColor first = Qt::black, last = Qt::white;
        double firstAt = 1e9, lastAt = -1e9;
        for (const QVariant& stop : stops) {
            const QVariantMap s = stop.toMap();
            const double at = valueOf(s, "Lctn");
            const QColor c = media::psdColor(s.value(QStringLiteral("Clr ")), Qt::black);
            if (at < firstAt) { firstAt = at; first = c; }
            if (at > lastAt) { lastAt = at; last = c; }
        }
        if (flag(gradient, "Rvrs", false)) std::swap(first, last);
        const double radians = valueOf(gradient, "Angl", 90.0) * kPi / 180.0;
        const double gradientScale = valueOf(gradient, "Scl ", 100.0) / 100.0;
        const double reach = (std::abs(std::cos(radians)) * layerSize.width() / 2.0
                              + std::abs(std::sin(radians)) * layerSize.height() / 2.0) * gradientScale;
        const QPointF direction(std::cos(radians), std::sin(radians));
        const bool radial = gradient.value(QStringLiteral("Type")).toString().startsWith(QLatin1String("Rdl"));
        const QPointF start = radial ? QPointF() : -direction * reach;
        const QPointF end = radial ? direction * reach : direction * reach;
        modeNote(gradient, PsdImportText::tr("Gradient Overlay"));
        add(makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.ColorGradient"), PsdImportText::tr("Gradient Overlay"),
                       {{QStringLiteral("startPoint"), QStringLiteral("%1,%2").arg(number(start.x()), number(start.y()))},
                        {QStringLiteral("endPoint"), QStringLiteral("%1,%2").arg(number(end.x()), number(end.y()))},
                        {QStringLiteral("startColor"), first.name()},
                        {QStringLiteral("endColor"), last.name()},
                        {QStringLiteral("isRadial"), radial ? QStringLiteral("true") : QStringLiteral("false")},
                        {QStringLiteral("blendStrength"), number(opacity(gradient))},
                        {QStringLiteral("tansferMode"), QStringLiteral("Normal")}}, notes));
    }
    if (!effectInstances(style, "patternFill", "patternFillMulti").isEmpty())
        addNote(notes, PsdImportText::tr("%1: Pattern Overlay is not converted.").arg(layerName));
    if (!effectInstances(style, "IrSh", "innerShadowMulti").isEmpty())
        addNote(notes, PsdImportText::tr("%1: Inner Shadow is not converted.").arg(layerName));
    if (!effectInstances(style, "ChFX", "satinMulti").isEmpty())
        addNote(notes, PsdImportText::tr("%1: Satin is not converted.").arg(layerName));
    if (!effectInstances(style, "ebbl", "bevelEmbossMulti").isEmpty())
        addNote(notes, PsdImportText::tr("%1: Bevel & Emboss is not converted.").arg(layerName));
    for (const QVariantMap& glow : effectInstances(style, "IrGl", "innerGlowMulti")) {
        add(makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.InnerGlow"), PsdImportText::tr("Inner Glow"),
                       {{QStringLiteral("color"), colour(glow, QColor(255, 255, 190))},
                        {QStringLiteral("opacity"), number(opacity(glow) / 100.0)},
                        {QStringLiteral("size"), number(valueOf(glow, "blur", 5.0) * scale)},
                        {QStringLiteral("spread"), number(qBound(0.0, valueOf(glow, "Ckmt"), 100.0))}}, notes));
    }
    // A stroke is drawn as a hard glow of its width: outside the layer, or
    // inside it for an inside stroke (a centred one straddles the edge).
    for (const QVariantMap& stroke : effectInstances(style, "FrFX", "frameFXMulti")) {
        const QString position = stroke.value(QStringLiteral("Styl")).toString();
        const double size = valueOf(stroke, "Sz  ", 3.0) * scale;
        if (stroke.value(QStringLiteral("PntT")).toString() != QLatin1String("SClr")
            && stroke.contains(QStringLiteral("PntT")))
            addNote(notes, PsdImportText::tr("%1: a gradient or pattern stroke is drawn in its colour.").arg(layerName));
        const bool inside = position == QLatin1String("InsF");
        add(makeEffect(specOf, inside ? QStringLiteral("com.FXHOME.HitFilm.InnerGlow")
                                      : QStringLiteral("com.FXHOME.HitFilm.OuterGlow"),
                       PsdImportText::tr("Stroke"),
                       {{QStringLiteral("color"), colour(stroke, Qt::red)},
                        {QStringLiteral("opacity"), number(opacity(stroke) / 100.0)},
                        {QStringLiteral("size"), number(position == QLatin1String("CtrF") ? size / 2.0 : size)},
                        {QStringLiteral("spread"), number(100.0)}}, notes));
    }
    for (const QVariantMap& glow : effectInstances(style, "OrGl", "outerGlowMulti")) {
        add(makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.OuterGlow"), PsdImportText::tr("Outer Glow"),
                       {{QStringLiteral("color"), colour(glow, QColor(255, 255, 190))},
                        {QStringLiteral("opacity"), number(opacity(glow) / 100.0)},
                        {QStringLiteral("size"), number(valueOf(glow, "blur", 5.0) * scale)},
                        {QStringLiteral("spread"), number(qBound(0.0, valueOf(glow, "Ckmt"), 100.0))}}, notes));
    }
    for (const QVariantMap& shadow : effectInstances(style, "DrSh", "dropShadowMulti")) {
        const double light = flag(shadow, "uglg", true) ? double(globalLightAngle) : valueOf(shadow, "lagl", 120.0);
        const double distance = valueOf(shadow, "Dstn", 5.0) * scale;
        const double blur = valueOf(shadow, "blur", 5.0) * scale;
        add(makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.DropShadow"), PsdImportText::tr("Drop Shadow"),
                       {{QStringLiteral("angle"), number(dropShadowAngle(light))},
                        {QStringLiteral("distance"), QString::number(qRound(distance))},
                        {QStringLiteral("distance_f"), number(distance)},
                        {QStringLiteral("shadowColor"), colour(shadow, Qt::black)},
                        {QStringLiteral("opacity"), number(opacity(shadow) / 100.0)},
                        {QStringLiteral("radius"), QString::number(qRound(blur))},
                        {QStringLiteral("radius_f"), number(blur)}}, notes));
    }
    return effects;
}

QVector<composition::Effect> psdSmartFilterEffects(const QVariantMap& smartObject, const EffectSpecLookup& specOf,
                                                   const QString& layerName, bool* complete, QStringList* notes)
{
    QVector<composition::Effect> effects;
    bool all = true;
    const QVariantMap filterFX = smartObject.value(QStringLiteral("filterFX")).toMap();
    if (!filterFX.isEmpty() && flag(filterFX, "enab", true)) {
        for (const QVariant& entry : filterFX.value(QStringLiteral("filterFXList")).toList()) {
            const QVariantMap item = entry.toMap();
            if (!flag(item, "enab", true)) continue;
            const QVariantMap filter = item.value(QStringLiteral("Fltr")).toMap();
            const QString name = item.value(QStringLiteral("Nm  ")).toString();
            const QVariantMap blend = item.value(QStringLiteral("blendOptions")).toMap();
            if (valueOf(blend, "Opct", 100.0) < 99.5
                || (blend.contains(QStringLiteral("Md  "))
                    && blend.value(QStringLiteral("Md  ")).toString() != QLatin1String("Nrml")))
                addNote(notes, PsdImportText::tr("%1: the blending options of the smart filter \"%2\" are not kept.")
                                   .arg(layerName, name));
            if (isCameraRawFilter(item, filter)) {
                const CameraRaw raw = cameraRawSettings(filter);
                const auto v = [&raw](const char* key) { return number(raw.value(QLatin1String(key))); };
                const auto made = makeEffect(
                    specOf, QStringLiteral("com.FXHOME.HitFilm.ExposurePro"), PsdImportText::tr("Camera Raw"),
                    {{QStringLiteral("exposure"), number(qBound(-4.0, raw.value(QStringLiteral("Exposure2012")), 4.0))},
                     {QStringLiteral("contrast"), v("Contrast2012")}, {QStringLiteral("highlights"), v("Highlights2012")},
                     {QStringLiteral("shadows"), v("Shadows2012")},   {QStringLiteral("whites"), v("Whites2012")},
                     {QStringLiteral("blacks"), v("Blacks2012")},     {QStringLiteral("vibrance"), v("Vibrance")},
                     {QStringLiteral("saturation"), v("Saturation")}},
                    notes);
                if (made) effects.append(*made);
                else all = false;
                if (!qFuzzyIsNull(raw.value(QStringLiteral("Dehaze")))) {
                    const auto dehaze = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Dehaze"),
                                                   PsdImportText::tr("Camera Raw Dehaze"),
                                                   {{QStringLiteral("amount"), v("Dehaze")}}, notes);
                    if (dehaze) effects.append(*dehaze);
                }
                // Anything more and the layer keeps the pixels Photoshop
                // filtered, so that it looks as it does there.
                QStringList unconverted = cameraRawUnconverted(filter);
                // Settings carried as XMP rather than descriptor keys.
                for (const auto& [key, setting] : {std::pair{"Clarity2012", "Clarity"}, std::pair{"Texture", "Texture"},
                                                   std::pair{"Temperature", "Temperature"}, std::pair{"Tint", "Tint"}}) {
                    if (!qFuzzyIsNull(raw.value(QLatin1String(key))) && !unconverted.contains(translated(setting)))
                        unconverted.append(translated(setting));
                }
                if (!unconverted.isEmpty()) {
                    all = false;
                    addNote(notes, PsdImportText::tr("%1: Camera Raw's %2 is not converted.")
                                       .arg(layerName, unconverted.join(QStringLiteral(", "))));
                }
            } else if (filter.value(QStringLiteral("_class")).toString() == QLatin1String("GsnB")) {
                // Three passes of a box blur come close to a Gaussian of
                // that radius.
                const double radius = valueOf(filter, "Rds ", 1.0);
                const auto made = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Blur"),
                                             name.isEmpty() ? PsdImportText::tr("Gaussian Blur") : name,
                                             {{QStringLiteral("radius"), QString::number(qRound(radius))},
                                              {QStringLiteral("radius_f"), number(radius)},
                                              {QStringLiteral("iterations"), QStringLiteral("3")}}, notes);
                if (made) effects.append(*made);
                else all = false;
            } else {
                all = false;
                addNote(notes, PsdImportText::tr("%1: the smart filter \"%2\" has no equivalent; the layer keeps its filtered pixels.")
                                   .arg(layerName, name.isEmpty() ? filter.value(QStringLiteral("_class")).toString() : name));
            }
        }
    }
    if (complete) *complete = all;
    return effects;
}

namespace {

// The effect a Grade layer gets for an adjustment layer.
std::optional<composition::Effect> adjustmentEffect(const media::PsdLayer& layer, const EffectSpecLookup& specOf,
                                                    QStringList* notes)
{
    const QByteArray& d = layer.adjustmentData;
    const auto u16 = [&d](int at) { return at + 2 <= d.size() ? qFromBigEndian<quint16>(d.constData() + at) : quint16(0); };
    const auto i16 = [&](int at) { return qint16(u16(at)); };
    const auto f32 = [&d](int at) {
        float value = 0.0f;
        if (at + 4 <= d.size()) {
            const quint32 bits = qFromBigEndian<quint32>(d.constData() + at);
            std::memcpy(&value, &bits, sizeof value);
        }
        return double(value);
    };
    // Descriptor-based adjustments start with a version; writers differ in
    // how many bytes of it there are.
    const auto descriptor = [&d]() {
        for (const qint64 offset : {qint64(4), qint64(0), qint64(8)}) {
            bool ok = false;
            const QVariantMap map = media::readPsdDescriptor(d, offset, &ok);
            if (ok && !map.value(QStringLiteral("_class")).toString().isEmpty()) return map;
        }
        return QVariantMap();
    };
    const QString& key = layer.adjustmentKey;
    if (key == QLatin1String("expA")) {
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Exposure"), PsdImportText::tr("Exposure"),
                          {{QStringLiteral("exposure"), number(f32(2))}, {QStringLiteral("offset"), number(f32(6))},
                           {QStringLiteral("gamma"), number(f32(10))}}, notes);
    }
    if (key == QLatin1String("CgEd") || key == QLatin1String("brit")) {
        double brightness = 0.0, contrast = 0.0;
        if (key == QLatin1String("CgEd")) {
            const QVariantMap values = descriptor();
            brightness = valueOf(values, "Brgh");
            contrast = valueOf(values, "Cntr");
        } else {
            brightness = i16(0);
            contrast = i16(2);
        }
        // Photoshop's brightness runs -150..150.
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.BrightnessContrast"), PsdImportText::tr("Brightness/Contrast"),
                          {{QStringLiteral("brightness"), number(qBound(-100.0, brightness * 100.0 / 150.0, 100.0))},
                           {QStringLiteral("contrast"), number(qBound(-100.0, contrast, 100.0))}}, notes);
    }
    if (key == QLatin1String("hue2")) {
        if (d.size() >= 4 && d.at(2) != 0) {
            addNote(notes, PsdImportText::tr("%1: Hue/Saturation's Colorize is not converted.").arg(layer.name));
            return std::nullopt;
        }
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.HSL"), PsdImportText::tr("Hue/Saturation"),
                          {{QStringLiteral("shift"), number(i16(10))}, {QStringLiteral("saturation"), number(i16(12))},
                           {QStringLiteral("lightness"), number(i16(14))}}, notes);
    }
    if (key == QLatin1String("levl")) {
        // Records of input floor and ceiling, output floor and ceiling and
        // gamma x 100: the composite first, then red, green and blue.
        QHash<QString, QString> values;
        const char* prefixes[] = {"", "red", "green", "blue"};
        for (int channel = 0; channel < 4; ++channel) {
            const int at = 2 + channel * 10;
            if (at + 10 > d.size()) break;
            const QString p = QString::fromLatin1(prefixes[channel]);
            const auto name = [&p](const char* base) {
                QString key = QString::fromLatin1(base);
                if (!p.isEmpty()) key[0] = key.at(0).toUpper();
                return p + key;
            };
            values.insert(name("inputBlack"), number(i16(at) / 255.0));
            values.insert(name("inputWhite"), number(i16(at + 2) / 255.0));
            values.insert(name("outputBlack"), number(i16(at + 4) / 255.0));
            values.insert(name("outputWhite"), number(i16(at + 6) / 255.0));
            values.insert(name("gamma"), number(qMax(1, int(u16(at + 8))) / 100.0));
        }
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Levels"), PsdImportText::tr("Levels"), values, notes);
    }
    if (key == QLatin1String("vibA")) {
        const QVariantMap values = descriptor();
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.ExposurePro"), PsdImportText::tr("Vibrance"),
                          {{QStringLiteral("vibrance"), number(valueOf(values, "vibrance"))},
                           {QStringLiteral("saturation"), number(valueOf(values, "Strt"))}}, notes);
    }
    if (key == QLatin1String("nvrt"))
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Invert"), PsdImportText::tr("Invert"), {}, notes);
    if (key == QLatin1String("post")) {
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Posterize"), PsdImportText::tr("Posterize"),
                          {{QStringLiteral("posterize"), number(qBound(2, int(u16(0)), 256))},
                           {QStringLiteral("fillBlur"), QStringLiteral("0")},
                           {QStringLiteral("smoothPosterize"), QStringLiteral("0")}}, notes);
    }
    if (key == QLatin1String("thrs")) {
        return makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.Threshold"), PsdImportText::tr("Threshold"),
                          {{QStringLiteral("threshold"), number(qBound(1.0, u16(0) * 100.0 / 255.0, 100.0))}}, notes);
    }
    static const QHash<QString, const char*> names = {
        {QStringLiteral("curv"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Curves")},
        {QStringLiteral("blnc"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Color Balance")},
        {QStringLiteral("selc"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Selective Color")},
        {QStringLiteral("mixr"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Channel Mixer")},
        {QStringLiteral("grdm"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Gradient Map")},
        {QStringLiteral("phfl"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Photo Filter")},
        {QStringLiteral("blwh"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Black & White")},
        {QStringLiteral("clrL"), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Color Lookup")},
        {QStringLiteral("hue "), QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Hue/Saturation")},
    };
    addNote(notes, PsdImportText::tr("%1: the adjustment \"%2\" is not converted.")
                       .arg(layer.name, names.contains(key) ? translated(names.value(key)) : key));
    return std::nullopt;
}

// A smart object's placement as a layer transform: the corners of its
// source in the document (Trnf: top left, top right, bottom right, bottom
// left). False when the placement is not a rectangle turned and scaled
// (perspective, warp).
bool smartObjectPlacement(const QVariantMap& smart, const QSize& sourceSize, const QSize& document,
                          composition::LayerTransform* transform)
{
    QVector<double> corners;
    for (const QVariant& v : smart.value(QStringLiteral("Trnf")).toList()) corners.append(media::psdNumber(v));
    if (corners.size() != 8 || sourceSize.isEmpty()) return false;
    QVector<double> nonAffine;
    for (const QVariant& v : smart.value(QStringLiteral("nonAffineTransform")).toList()) nonAffine.append(media::psdNumber(v));
    for (int i = 0; i < nonAffine.size() && i < 8; ++i)
        if (std::abs(nonAffine.at(i) - corners.at(i)) > 0.01) return false;
    const QString warp = smart.value(QStringLiteral("warp")).toMap().value(QStringLiteral("warpStyle")).toString();
    if (!warp.isEmpty() && warp != QLatin1String("warpNone")) return false;
    const QPointF tl(corners[0], corners[1]), tr(corners[2], corners[3]), br(corners[4], corners[5]),
        bl(corners[6], corners[7]);
    // A parallelogram with right angles: a rectangle turned and scaled.
    const QLineF top(tl, tr), left(tl, bl);
    if (std::abs(QLineF(tr, br).length() - left.length()) > 0.5 || std::abs(QLineF(bl, br).length() - top.length()) > 0.5)
        return false;
    if (std::abs(std::abs(top.angleTo(left)) - 270.0) > 0.5 && std::abs(std::abs(top.angleTo(left)) - 90.0) > 0.5)
        return false;
    const QPointF centre = (tl + br) / 2.0;
    transform->position = QPointF(centre.x() - document.width() / 2.0, document.height() / 2.0 - centre.y());
    transform->scalePercent = QPointF(top.length() / sourceSize.width() * 100.0,
                                      left.length() / sourceSize.height() * 100.0);
    transform->rotationDegrees = std::atan2(tr.y() - tl.y(), tr.x() - tl.x()) * 180.0 / kPi;
    return true;
}

// ------------------------------------------------------- look-up tables

// A 3D look-up table of size^3 RGB entries in 0..1, red fastest - the order
// .cube files keep, which the LUT effect reads.
struct CubeLut
{
    int size = 33;
    QVector<float> rgb;
};

// Writes a table as a .cube file named after what it stands for and its
// content, so that one table is one file however often it is imported.
// Returns its path; empty when it cannot be written.
QString writeCubeLut(const CubeLut& lut, const QString& directory, const QString& title)
{
    QByteArray text = "TITLE \"" + title.toUtf8().replace('"', '\'') + "\"\nLUT_3D_SIZE "
                      + QByteArray::number(lut.size) + "\n";
    for (int i = 0; i + 2 < lut.rgb.size(); i += 3) {
        text += QByteArray::number(lut.rgb.at(i), 'f', 6) + ' ' + QByteArray::number(lut.rgb.at(i + 1), 'f', 6) + ' '
                + QByteArray::number(lut.rgb.at(i + 2), 'f', 6) + '\n';
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha1).toHex().left(10));
    QString base = title;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    if (!QDir().mkpath(directory)) return {};
    const QString path = QDir(directory).filePath(base.left(80) + QLatin1Char(' ') + hash + QStringLiteral(".cube"));
    if (!QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size()) return {};
    }
    return QFileInfo(path).absoluteFilePath();
}

// A curve through its points as Photoshop draws it - a natural cubic spline,
// flat beyond the end points and clamped to 0..255 - as 256 values.
QVector<double> curveTable(QVector<QPointF> points)
{
    std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    for (int i = int(points.size()) - 1; i > 0; --i)
        if (points.at(i).x() <= points.at(i - 1).x()) points.removeAt(i);
    QVector<double> table(256);
    if (points.size() < 2) {
        for (int v = 0; v < 256; ++v) table[v] = v;
        return table;
    }
    const int n = int(points.size());
    // Second derivatives, zero at the ends.
    QVector<double> second(n, 0.0), u(n, 0.0);
    for (int i = 1; i < n - 1; ++i) {
        const double sig = (points[i].x() - points[i - 1].x()) / (points[i + 1].x() - points[i - 1].x());
        const double pp = sig * second[i - 1] + 2.0;
        second[i] = (sig - 1.0) / pp;
        const double d = (points[i + 1].y() - points[i].y()) / (points[i + 1].x() - points[i].x())
                         - (points[i].y() - points[i - 1].y()) / (points[i].x() - points[i - 1].x());
        u[i] = (6.0 * d / (points[i + 1].x() - points[i - 1].x()) - sig * u[i - 1]) / pp;
    }
    for (int k = n - 2; k >= 0; --k) second[k] = second[k] * second[k + 1] + u[k];
    for (int v = 0; v < 256; ++v) {
        double y = 0.0;
        if (v <= points.first().x()) {
            y = points.first().y();
        } else if (v >= points.last().x()) {
            y = points.last().y();
        } else {
            int k = 0;
            while (k + 1 < n - 1 && points[k + 1].x() < v) ++k;
            const double h = points[k + 1].x() - points[k].x();
            const double a = (points[k + 1].x() - v) / h, b = (v - points[k].x()) / h;
            y = a * points[k].y() + b * points[k + 1].y()
                + ((a * a * a - a) * second[k] + (b * b * b - b) * second[k + 1]) * h * h / 6.0;
        }
        table[v] = qBound(0.0, y, 255.0);
    }
    return table;
}

// The Curves adjustment ('curv'): a pad byte, the version, a bitmap of the
// curves present (composite, red, green, blue, ...) and each curve's points
// as (output, input) pairs. The composite curve goes first, then each
// channel's.
std::optional<CubeLut> curvesLut(const QByteArray& d)
{
    const auto u16 = [&d](int at) { return at + 2 <= d.size() ? int(qFromBigEndian<quint16>(d.constData() + at)) : -1; };
    if (d.size() < 9) return std::nullopt;
    int at = 3;
    const quint32 bitmap = qFromBigEndian<quint32>(d.constData() + at);
    at += 4;
    QVector<QVector<double>> tables(4);
    bool any = false;
    for (int curve = 0; curve < 32; ++curve) {
        if (!(bitmap & (1u << curve))) continue;
        const int count = u16(at);
        at += 2;
        if (count < 2 || count > 64 || at + count * 4 > d.size()) return std::nullopt;
        QVector<QPointF> points;
        for (int i = 0; i < count; ++i, at += 4) points.append(QPointF(u16(at + 2), u16(at)));
        if (curve < 4) {
            tables[curve] = curveTable(points);
            any = true;
        }
    }
    if (!any) return std::nullopt;
    const auto apply = [&tables](int table, double v) {
        if (tables.at(table).isEmpty()) return v;
        const int i = qBound(0, int(v), 254);
        const double f = qBound(0.0, v - i, 1.0);
        return tables.at(table).at(i) * (1.0 - f) + tables.at(table).at(i + 1) * f;
    };
    CubeLut lut;
    const int n = lut.size;
    lut.rgb.reserve(n * n * n * 3);
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const int in[3] = {r, g, b};
                for (int c = 0; c < 3; ++c)
                    lut.rgb.append(float(apply(1 + c, apply(0, in[c] * 255.0 / (n - 1))) / 255.0));
            }
    return lut;
}

// A table that takes a smart object's source to what Photoshop's filters
// made of it, learned from the two pictures: `source` - the source's own
// flattened image - placed in the document by `toDocument`, and `filtered`
// - the layer's pixels, at `origin` in the document. Each pair adds to the
// eight entries around the source colour; entries no colour reached take
// their neighbours' mean, those still empty stay as they are. With
// `maxRadius`, only pixels within that elliptical radius of `frame`'s centre
// count (1 at the middle of its edges): what a vignette leaves alone.
std::optional<CubeLut> fittedLut(const QImage& sourceImage, const QTransform& toDocument, const QImage& filteredImage,
                                 const QPoint& origin, const QRectF& frame = {}, double maxRadius = 0.0)
{
    bool invertible = false;
    const QTransform toSource = toDocument.inverted(&invertible);
    if (!invertible || sourceImage.isNull() || filteredImage.isNull()) return std::nullopt;
    const QImage source = sourceImage.convertToFormat(QImage::Format_RGBA8888);
    const QImage filtered = filteredImage.convertToFormat(QImage::Format_RGBA8888);
    const int n = 33;
    QVector<double> weight(n * n * n, 0.0), sum(n * n * n * 3, 0.0);
    qint64 pairs = 0;
    for (int y = 0; y < filtered.height(); ++y) {
        const uchar* row = filtered.constScanLine(y);
        for (int x = 0; x < filtered.width(); ++x) {
            if (row[x * 4 + 3] < 255) continue;
            if (maxRadius > 0.0) {
                const double dx = (origin.x() + x + 0.5 - frame.center().x()) / (frame.width() / 2.0);
                const double dy = (origin.y() + y + 0.5 - frame.center().y()) / (frame.height() / 2.0);
                if (dx * dx + dy * dy > maxRadius * maxRadius) continue;
            }
            const QPointF at = toSource.map(QPointF(origin.x() + x + 0.5, origin.y() + y + 0.5));
            const int sx = int(std::floor(at.x())), sy = int(std::floor(at.y()));
            if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) continue;
            const uchar* s = source.constScanLine(sy) + sx * 4;
            if (s[3] < 255) continue;
            double cell[3], frac[3];
            int base[3];
            for (int c = 0; c < 3; ++c) {
                cell[c] = s[c] * (n - 1) / 255.0;
                base[c] = qMin(n - 2, int(cell[c]));
                frac[c] = cell[c] - base[c];
            }
            for (int corner = 0; corner < 8; ++corner) {
                double w = 1.0;
                int index = 0, stride = 1;
                for (int c = 0; c < 3; ++c) {
                    const bool up = corner & (1 << c);
                    w *= up ? frac[c] : 1.0 - frac[c];
                    index += (base[c] + (up ? 1 : 0)) * stride;
                    stride *= n;
                }
                if (w <= 0.0) continue;
                weight[index] += w;
                for (int c = 0; c < 3; ++c) sum[index * 3 + c] += w * row[x * 4 + c] / 255.0;
            }
            ++pairs;
        }
    }
    if (pairs < 1000) return std::nullopt;
    CubeLut lut;
    lut.size = n;
    lut.rgb.resize(n * n * n * 3);
    QVector<bool> known(n * n * n, false);
    for (int i = 0; i < n * n * n; ++i) {
        if (weight.at(i) < 1e-3) continue;
        for (int c = 0; c < 3; ++c) lut.rgb[i * 3 + c] = float(sum.at(i * 3 + c) / weight.at(i));
        known[i] = true;
    }
    for (int pass = 0; pass < 3 * n; ++pass) {
        QVector<bool> next = known;
        bool changed = false;
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g)
                for (int r = 0; r < n; ++r) {
                    const int i = r + g * n + b * n * n;
                    if (known.at(i)) continue;
                    double acc[3] = {0, 0, 0};
                    int count = 0;
                    const int neighbours[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                    for (const auto& d : neighbours) {
                        const int rr = r + d[0], gg = g + d[1], bb = b + d[2];
                        if (rr < 0 || gg < 0 || bb < 0 || rr >= n || gg >= n || bb >= n) continue;
                        const int j = rr + gg * n + bb * n * n;
                        if (!known.at(j)) continue;
                        // A neighbour's output, moved by the step between the
                        // two inputs, keeps the table's slope.
                        const double step[3] = {-d[0] / double(n - 1), -d[1] / double(n - 1), -d[2] / double(n - 1)};
                        for (int c = 0; c < 3; ++c) acc[c] += lut.rgb.at(j * 3 + c) + step[c];
                        ++count;
                    }
                    if (count == 0) continue;
                    for (int c = 0; c < 3; ++c) lut.rgb[i * 3 + c] = float(qBound(0.0, acc[c] / count, 1.0));
                    next[i] = true;
                    changed = true;
                }
        known = next;
        if (!changed) break;
    }
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const int i = r + g * n + b * n * n;
                if (known.at(i)) continue;
                const int in[3] = {r, g, b};
                for (int c = 0; c < 3; ++c) lut.rgb[i * 3 + c] = float(in[c] / double(n - 1));
            }
    return lut;
}

// Brightness/Contrast as Photoshop draws it since CS3 (not "Use Legacy"): a
// curve that keeps black and white where they are - brightness moves the
// middle, contrast bends the quarter tones around it - where the legacy
// mode, like the reference's Brightness & Contrast, scales linearly around
// the middle and clips. The shape is an estimate (Photoshop's own formula is
// not published): the quarter tones move by a fifth of the contrast, the
// middle by 0.45 of the brightness. Null for the legacy mode.
std::optional<CubeLut> modernBrightnessContrastLut(const QByteArray& data)
{
    QVariantMap values;
    for (const qint64 offset : {qint64(4), qint64(0), qint64(8)}) {
        bool ok = false;
        values = media::readPsdDescriptor(data, offset, &ok);
        if (ok && values.contains(QStringLiteral("Cntr"))) break;
        values.clear();
    }
    if (values.isEmpty() || flag(values, "useLegacy", false)) return std::nullopt;
    const double brightness = valueOf(values, "Brgh"), contrast = valueOf(values, "Cntr");
    const QVector<double> bright = curveTable({QPointF(0, 0), QPointF(128, qBound(16.0, 128 + 0.45 * brightness, 239.0)),
                                               QPointF(255, 255)});
    const double d = contrast * 0.2;
    const QVector<double> contrasted = curveTable({QPointF(0, 0), QPointF(64, qBound(1.0, 64 - d, 127.0)),
                                                   QPointF(128, 128), QPointF(192, qBound(129.0, 192 + d, 254.0)),
                                                   QPointF(255, 255)});
    const auto at = [](const QVector<double>& table, double v) {
        const int i = qBound(0, int(v), 254);
        const double f = qBound(0.0, v - i, 1.0);
        return table.at(i) * (1.0 - f) + table.at(i + 1) * f;
    };
    CubeLut lut;
    const int n = lut.size;
    lut.rgb.reserve(n * n * n * 3);
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const int in[3] = {r, g, b};
                for (int c = 0; c < 3; ++c)
                    lut.rgb.append(float(at(contrasted, at(bright, in[c] * 255.0 / (n - 1))) / 255.0));
            }
    return lut;
}

// Photoshop's Black & White: the grey of a colour is its lowest channel,
// plus the step to the middle one weighted by the secondary hue it makes
// with the highest (yellow, cyan or magenta), plus the step to the highest
// weighted by that primary (red, green or blue) - the six sliders, in
// percent. Its tint is not carried over (noted).
std::optional<CubeLut> blackWhiteLut(const QByteArray& data, bool* tinted)
{
    QVariantMap values;
    for (const qint64 offset : {qint64(4), qint64(0), qint64(8)}) {
        bool ok = false;
        values = media::readPsdDescriptor(data, offset, &ok);
        if (ok && values.contains(QStringLiteral("Rd  "))) break;
        values.clear();
    }
    if (values.isEmpty()) return std::nullopt;
    if (tinted) *tinted = flag(values, "useTint", false);
    const double red = valueOf(values, "Rd  ", 40) / 100.0, yellow = valueOf(values, "Yllw", 60) / 100.0;
    const double green = valueOf(values, "Grn ", 40) / 100.0, cyan = valueOf(values, "Cyn ", 60) / 100.0;
    const double blue = valueOf(values, "Bl  ", 20) / 100.0, magenta = valueOf(values, "Mgnt", 80) / 100.0;
    CubeLut lut;
    const int n = lut.size;
    lut.rgb.reserve(n * n * n * 3);
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const double c[3] = {r / double(n - 1), g / double(n - 1), b / double(n - 1)};
                int hi = 0, lo = 0;
                for (int k = 1; k < 3; ++k) {
                    if (c[k] > c[hi]) hi = k;
                    if (c[k] < c[lo]) lo = k;
                }
                if (hi == lo) lo = (hi + 1) % 3;
                const int mid = 3 - hi - lo;
                const double primary = hi == 0 ? red : hi == 1 ? green : blue;
                // The secondary between the highest and the middle channel.
                const int pair = (1 << hi) | (1 << mid);
                const double secondary = pair == 3 ? yellow : pair == 6 ? cyan : magenta;
                const double grey = qBound(0.0, c[lo] + (c[mid] - c[lo]) * secondary + (c[hi] - c[mid]) * primary, 1.0);
                for (int k = 0; k < 3; ++k) lut.rgb.append(float(grey));
            }
    return lut;
}

// Where a smart object's source lies in the document: its corners (Trnf)
// as a transform from the source's pixels.
std::optional<QTransform> smartObjectTransform(const QVariantMap& smart, const QSize& sourceSize)
{
    QVector<double> c;
    for (const QVariant& v : smart.value(QStringLiteral("Trnf")).toList()) c.append(media::psdNumber(v));
    if (c.size() != 8 || sourceSize.isEmpty()) return std::nullopt;
    const double w = sourceSize.width(), h = sourceSize.height();
    return QTransform((c[2] - c[0]) / w, (c[3] - c[1]) / w, (c[6] - c[0]) / h, (c[7] - c[1]) / h, c[0], c[1]);
}

// Whether every smart filter that is on is Camera Raw.
bool onlyCameraRaw(const QVariantMap& smartObject)
{
    const QVariantMap filterFX = smartObject.value(QStringLiteral("filterFX")).toMap();
    bool any = false;
    for (const QVariant& entry : filterFX.value(QStringLiteral("filterFXList")).toList()) {
        const QVariantMap item = entry.toMap();
        if (!flag(item, "enab", true)) continue;
        if (!isCameraRawFilter(item, item.value(QStringLiteral("Fltr")).toMap())) return false;
        any = true;
    }
    return any;
}

} // namespace

namespace {

// What one document's import needs besides the document.
struct ImportContext
{
    media::MediaManager& media;
    const EffectSpecLookup& specOf;
    const PsdImportOptions& options;
    QString lookDirectory;
    PsdImportResult* result;
};

// What hides parts of a layer, as a shape: the intersection of its own
// vector mask, its groups' and - for a layer clipped to a base that is solid
// inside its outline - that base's outline, in document pixels.
struct MaskShape
{
    bool masked = false;      // false: nothing hides any of it
    QPainterPath path;        // what shows; empty and masked: nothing does
};

// Whether a layer's pixels are solid inside `outline` and empty outside it,
// so that clipping to the layer is clipping to the outline. Sampled every
// few pixels; a few antialiased edge pixels may differ.
bool solidInside(const media::PsdDocument& document, int index, const QPainterPath& outline)
{
    const QRect rect = document.layers().at(index).rect;
    const QImage image = document.layerImage(index);
    if (image.isNull() || rect.isEmpty()) return false;
    QImage coverage(rect.size(), QImage::Format_Alpha8);
    coverage.fill(0);
    {
        QPainter painter(&coverage);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-rect.topLeft());
        painter.fillPath(outline, Qt::white);
    }
    const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888);
    qint64 samples = 0, wrong = 0;
    for (int y = 0; y < rect.height(); y += 3) {
        const uchar* row = pixels.constScanLine(y);
        const uchar* cover = coverage.constScanLine(y);
        for (int x = 0; x < rect.width(); x += 3) {
            ++samples;
            if (std::abs(int(row[x * 4 + 3]) - int(cover[x])) > 32) ++wrong;
        }
    }
    return samples > 0 && wrong * 100 <= samples;
}

// The shape of what hides a layer (see MaskShape); null when a mask cannot be
// one - a raster mask that hides something, or a base that is not solid
// inside its outline.
std::optional<MaskShape> maskShapeOf(const media::PsdDocument& document, int index, int base)
{
    MaskShape shape;
    const auto take = [&](int record) {
        if (!document.rasterMaskHidesNothing(record)) return false;
        const QPainterPath path = document.vectorMaskPath(record);
        if (path.isEmpty()) return true;
        shape.path = shape.masked ? shape.path.intersected(path) : path;
        shape.masked = true;
        return true;
    };
    if (!take(index)) return std::nullopt;
    for (int group : document.enclosingGroups(index))
        if (!take(group)) return std::nullopt;
    if (base >= 0) {
        if (!document.rasterMaskHidesNothing(base)) return std::nullopt;
        QPainterPath outline = document.vectorMaskPath(base);
        if (outline.isEmpty()) outline.addRect(QRectF(document.layers().at(base).rect));
        if (!solidInside(document, base, outline)) return std::nullopt;
        shape.path = shape.masked ? shape.path.intersected(outline) : outline;
        shape.masked = true;
    }
    return shape;
}

// A shape as this port's masks: one freehand mask per polygon it fills.
QVector<composition::LayerMask> layerMasksOf(const QPainterPath& path, const QString& name)
{
    QVector<composition::LayerMask> masks;
    for (const QPolygonF& polygon : path.toFillPolygons()) {
        if (polygon.size() < 3) continue;
        composition::LayerMask mask;
        mask.name = name;
        mask.shape = composition::MaskShape::Freehand;
        mask.points = QVector<QPointF>(polygon.cbegin(), polygon.cend());
        mask.bounds = polygon.boundingRect();
        masks.append(mask);
    }
    return masks;
}

// Puts a shape on a layer: its masks, or the layer hidden when the shape
// shows nothing.
void applyMaskShape(composition::Layer& layer, const MaskShape& shape)
{
    if (!shape.masked) return;
    layer.masks = layerMasksOf(shape.path, PsdImportText::tr("Photoshop Mask"));
    if (layer.masks.isEmpty()) layer.visible = false;
}

// A layer's masks as one picture mask (media::kPsdMaskMarker): its raster
// and vector masks, its groups' and, clipped, its base's alpha - exact
// where shapes cannot be.
void applyPictureMask(composition::Layer& layer, const media::PsdDocument& document,
                      const QString& documentPath, int index, int base)
{
    QString path = documentPath + QLatin1String(media::kPsdMaskMarker)
                   + QString::number(document.layers().at(index).id);
    if (base >= 0)
        path += QLatin1String(media::kPsdClipMarker) + QString::number(document.layers().at(base).id);
    composition::LayerMask mask;
    mask.name = PsdImportText::tr("Photoshop Mask");
    mask.shape = composition::MaskShape::Image;
    mask.imagePath = path;
    mask.bounds = QRectF(document.maskArea(index));
    layer.masks = {mask};
}

// The mean difference of two pictures over the colour channels of 8 x 8
// blocks' averages, both over white (film grain counts by its average);
// negative when they cannot be compared.
double blockDifference(const QImage& first, const QImage& second)
{
    if (first.isNull() || second.isNull() || first.size() != second.size()) return -1.0;
    const auto overWhite = [](const QImage& image) {
        QImage white(image.size(), QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QPainter painter(&white);
        painter.drawImage(0, 0, image);
        painter.end();
        return white;
    };
    const QImage a = overWhite(first), b = overWhite(second);
    double total = 0.0;
    qint64 samples = 0;
    for (int by = 0; by + 8 <= a.height(); by += 8) {
        for (int bx = 0; bx + 8 <= a.width(); bx += 8) {
            double sa[3] = {0, 0, 0}, sb[3] = {0, 0, 0};
            for (int y = by; y < by + 8; ++y) {
                const uchar* pa = a.constScanLine(y) + bx * 4;
                const uchar* pb = b.constScanLine(y) + bx * 4;
                for (int x = 0; x < 8; ++x)
                    for (int c = 0; c < 3; ++c) { sa[c] += pa[x * 4 + c]; sb[c] += pb[x * 4 + c]; }
            }
            for (int c = 0; c < 3; ++c) total += std::abs(sa[c] - sb[c]) / 64.0;
            samples += 3;
        }
    }
    return samples > 0 ? total / samples : -1.0;
}

// An embedded document split into layers draws within this of Photoshop's
// own picture of it (levels, per 8 x 8 block), or it is not split.
constexpr double kMaxSplitDifference = 6.0;

QString defaultLookDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return (base.isEmpty() ? QDir::tempPath() : base) + QStringLiteral("/Photoshop Looks");
}

// Embedded documents go this deep into each other at most.
constexpr int kMaxNesting = 4;

// One document as a composite shot: `documentPath` is the file, or the path
// of a document embedded in one (media::openPsdDocument). The layers of an
// embedded one are locked: they are the source of a smart object, edited
// through it.
std::shared_ptr<composition::Composition> importDocument(const media::PsdDocument& document,
                                                         const QString& documentPath, const QString& name, int depth,
                                                         ImportContext& context)
{
    const PsdImportOptions& options = context.options;
    const EffectSpecLookup& specOf = context.specOf;
    media::MediaManager& media = context.media;
    QStringList* notes = &context.result->notes;
    auto shot = std::make_shared<composition::Composition>();
    shot->setName(name);
    shot->setSize(document.size().width(), document.size().height());
    shot->setFrameRate(options.fpsNumerator, qMax(1, options.fpsDenominator));
    shot->setDurationSeconds(options.durationSeconds);
    shot->setAudioSampleRate(options.audioSampleRate);
    const bool locked = depth > 0;

    const QVector<media::PsdLayer>& records = document.layers();
    const QVector<bool> shown = media::psdLayersShown(document);
    // Group opacity and blend mode, walked from the top like the visibility.
    QVector<double> groupOpacity(records.size(), 1.0);
    {
        QVector<double> stack{1.0};
        for (int i = int(records.size()) - 1; i >= 0; --i) {
            const media::PsdLayer& layer = records.at(i);
            groupOpacity[i] = stack.last();
            if (layer.kind == media::PsdLayer::Kind::GroupStart) {
                stack.append(stack.last() * layer.opacity / 255.0);
                if (layer.blendKey != QLatin1String("pass") && layer.blendKey != QLatin1String("norm"))
                    addNote(notes, PsdImportText::tr("The group \"%1\": its blend mode \"%2\" is not kept.")
                                       .arg(layer.name, photoshopModeName(layer.blendKey)));
            } else if (layer.kind == media::PsdLayer::Kind::GroupEnd && stack.size() > 1) {
                stack.removeLast();
            }
        }
    }

    const auto placeAtRect = [&document](composition::Layer& layer, const QRect& rect) {
        const QPointF centre = QRectF(rect).center();
        layer.transform.position = QPointF(centre.x() - document.size().width() / 2.0,
                                           document.size().height() / 2.0 - centre.y());
    };
    const auto clipOf = [&](const QString& assetPath) {
        composition::Clip clip;
        clip.startSeconds = 0.0;
        clip.durationSeconds = options.durationSeconds;
        if (!assetPath.isEmpty() && media.importFile(assetPath).isSuccess())
            clip.mediaId = media.assetByFilePath(assetPath).id();
        return clip;
    };
    // The unclipped pixel layer a clipped one is clipped to: the nearest
    // below it in its group, past the other clipped ones.
    const auto baseOf = [&records](int i) {
        for (int b = i - 1; b >= 0; --b) {
            const media::PsdLayer& below = records.at(b);
            if (below.kind == media::PsdLayer::Kind::GroupStart || below.kind == media::PsdLayer::Kind::GroupEnd)
                return -1;
            if (below.clipping) continue;
            return below.kind != media::PsdLayer::Kind::Adjustment && !below.rect.isEmpty() ? b : -1;
        }
        return -1;
    };

    QVector<composition::Layer> layers;
    // Layer styles go on after everything that changes the layer's own
    // pixels (its filters, the adjustments clipped to it).
    QVector<QVector<composition::Effect>> styles;
    QHash<int, int> layerOfRecord;
    for (int i = 0; i < records.size(); ++i) {
        const media::PsdLayer& record = records.at(i);
        if (record.kind == media::PsdLayer::Kind::GroupStart || record.kind == media::PsdLayer::Kind::GroupEnd)
            continue;
        composition::Layer layer;
        layer.name = record.name;
        layer.visible = shown.at(i);
        layer.locked = locked;
        layer.opacity = record.opacity / 255.0 * groupOpacity.at(i);
        bool exact = true;
        layer.blendMode = psdBlendModeName(record.blendKey, &exact);
        // (a hidden layer's mode changes nothing that shows)
        if (!exact && layer.visible)
            addNote(notes, PsdImportText::tr("%1: the blend mode \"%2\" is shown as \"%3\".")
                               .arg(record.name, photoshopModeName(record.blendKey),
                                    composition::blendModeDisplayName(layer.blendMode)));

        if (record.kind == media::PsdLayer::Kind::Adjustment) {
            std::optional<composition::Effect> effect;
            const auto modern = record.adjustmentKey == QLatin1String("CgEd")
                ? modernBrightnessContrastLut(record.adjustmentData) : std::nullopt;
            if (modern) {
                const QString path = writeCubeLut(*modern, context.lookDirectory,
                                                  QStringLiteral("%1 - %2").arg(name, record.name));
                if (!path.isEmpty())
                    effect = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.LUT"),
                                        PsdImportText::tr("Brightness/Contrast"),
                                        {{QStringLiteral("lutFilePath"), path}, {QStringLiteral("strength"), QStringLiteral("100")}},
                                        notes);
            }
            if (!effect && record.adjustmentKey == QLatin1String("blwh")) {
                bool tinted = false;
                if (const auto lut = blackWhiteLut(record.adjustmentData, &tinted)) {
                    const QString path = writeCubeLut(*lut, context.lookDirectory,
                                                      QStringLiteral("%1 - %2").arg(name, record.name));
                    if (!path.isEmpty())
                        effect = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.LUT"), translated(QT_TRANSLATE_NOOP("openvegas::project::PsdImport", "Black & White")),
                                            {{QStringLiteral("lutFilePath"), path}, {QStringLiteral("strength"), QStringLiteral("100")}},
                                            notes);
                    if (effect && tinted)
                        addNote(notes, PsdImportText::tr("%1: the tint of Black & White is not converted.").arg(record.name));
                }
            }
            if (!effect && record.adjustmentKey == QLatin1String("curv")) {
                // Curves, exactly, as a look-up table.
                if (const auto lut = curvesLut(record.adjustmentData)) {
                    const QString path = writeCubeLut(*lut, context.lookDirectory,
                                                      QStringLiteral("%1 - %2").arg(name, record.name));
                    if (!path.isEmpty())
                        effect = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.LUT"), PsdImportText::tr("Curves"),
                                            {{QStringLiteral("lutFilePath"), path}, {QStringLiteral("strength"), QStringLiteral("100")}},
                                            notes);
                }
                if (!effect)
                    addNote(notes, PsdImportText::tr("%1: the adjustment \"%2\" is not converted.")
                                       .arg(record.name, PsdImportText::tr("Curves")));
            } else if (!effect) {
                effect = adjustmentEffect(record, specOf, notes);
            }
            if (!effect) continue;
            // Its mask, as masks of the Grade layer: shapes when it is vector
            // ones, a picture otherwise (a white raster mask - every new
            // adjustment's - hides nothing).
            const auto shape = maskShapeOf(document, i, -1);
            if (record.clipping) {
                // Clipped to a pixel layer with only adjustments between, and
                // no mask of its own: an effect of that layer.
                const int base = baseOf(i);
                bool onlyAdjustments = base >= 0 && layerOfRecord.contains(base);
                for (int b = base + 1; onlyAdjustments && b < i; ++b)
                    onlyAdjustments = records.at(b).kind == media::PsdLayer::Kind::Adjustment;
                // (and at full opacity: an effect has none of its own)
                if (onlyAdjustments && shape && !shape->masked && layer.opacity >= 0.999) {
                    layers[layerOfRecord.value(base)].clips.first().effects.append(*effect);
                    continue;
                }
                // Otherwise a Grade layer masked by its own mask and the base's
                // alpha: what is below the base does not show where the base
                // is solid, so this grades the base alone - wherever it is
                // solid, and a little of what is below where it is not.
                if (base < 0) {
                    addNote(notes, PsdImportText::tr("%1: a clipped adjustment applies to everything below it.").arg(record.name));
                } else if (const auto clipped = maskShapeOf(document, i, base)) {
                    applyMaskShape(layer, *clipped);
                } else {
                    applyPictureMask(layer, document, documentPath, i, base);
                }
            } else if (shape) {
                applyMaskShape(layer, *shape);
            } else {
                applyPictureMask(layer, document, documentPath, i, -1);
            }
            layer.kind = composition::LayerKind::Grade;
            composition::Clip clip = clipOf(QString());
            clip.effects.append(*effect);
            layer.clips.append(clip);
            layers.append(layer);
            styles.append(QVector<composition::Effect>());
            ++context.result->layerCount;
            continue;
        }
        // A Solid Color fill layer saved without pixels: its colour over the
        // whole document, as a Plane, shaped by its masks.
        if (record.rect.isEmpty() && record.fillColor.isValid()) {
            layer.kind = composition::LayerKind::Plane;
            layer.planeColor = record.fillColor;
            composition::Clip clip = clipOf(QString());
            layer.clips.append(clip);
            const int base = record.clipping ? baseOf(i) : -1;
            if (const auto shape = maskShapeOf(document, i, base)) applyMaskShape(layer, *shape);
            else applyPictureMask(layer, document, documentPath, i, base);
            if (record.fillOpacity < 255) layer.opacity *= record.fillOpacity / 255.0;
            layerOfRecord.insert(i, int(layers.size()));
            layers.append(layer);
            styles.append(psdLayerStyleEffects(record.effects, document.globalLightAngle(), QSizeF(document.size()),
                                               specOf, record.name, notes));
            ++context.result->layerCount;
            continue;
        }
        if (record.rect.isEmpty()) continue;       // an empty layer

        const int base = record.clipping ? baseOf(i) : -1;
        layer.kind = composition::LayerKind::Media;
        QVector<composition::Effect> effects;
        // A Grade layer to go right above this one (Camera Raw's vignette).
        std::optional<composition::Effect> pendingVignette;

        // A smart object whose source is a Photoshop document, placed
        // without warp or masks: that document's own shot, its layers
        // locked, with the smart filters as effects. One with another
        // embedded source and filters that all have an equivalent here: the
        // unfiltered source with the filters as effects. Either way
        // editable; otherwise the layer keeps Photoshop's pixels.
        bool fromSource = false;
        const QVariantMap& smart = record.smartObject;
        const bool hasFilters = !smart.value(QStringLiteral("filterFX")).toMap().isEmpty();
        if (record.kind == media::PsdLayer::Kind::SmartObject) {
            const QString uid = smart.value(QStringLiteral("Idnt")).toString();
            const QString sourcePath = documentPath + QLatin1String(media::kPsdSourceMarker) + uid;
            const bool embedded = document.linkedFiles().contains(uid);
            const auto inner = embedded && depth + 1 <= kMaxNesting ? media::openPsdDocument(sourcePath) : nullptr;
            if (inner || hasFilters) {
                bool complete = true;
                QStringList filterNotes;
                QVector<composition::Effect> filters;
                if (hasFilters) filters = psdSmartFilterEffects(smart, specOf, record.name, &complete, &filterNotes);
                const QSize sourceSize = inner ? inner->size() : embedded ? media::stillImageSize(sourcePath) : QSize();
                composition::LayerTransform placement;
                const bool placed = sourceSize.isValid()
                    && smartObjectPlacement(smart, sourceSize, document.size(), &placement);
                // Its masks, its groups' and a base it is clipped to become
                // masks of the layer: shapes when they are vector ones, one
                // picture mask otherwise.
                const auto maskShape = maskShapeOf(document, i, base);
                // An embedded document is split into its own shot first: its
                // picture is what the filters work on here.
                std::shared_ptr<composition::Composition> nested;
                QImage drawnNested;
                bool splitRejected = false;
                if (inner && placed && (complete || onlyCameraRaw(smart))) {
                    const QString innerName =
                        QFileInfo(document.linkedFiles().value(uid).name.remove(QChar(0))).completeBaseName();
                    const qsizetype shotsBefore = context.result->nestedShots.size();
                    const qsizetype notesBefore = notes->size();
                    const int layersBefore = context.result->layerCount;
                    nested = importDocument(*inner, sourcePath, innerName.isEmpty() ? record.name : innerName,
                                            depth + 1, context);
                    // Kept split only when it draws as Photoshop drew the
                    // document; otherwise the document's own picture stands
                    // in for it (what it brought along goes too).
                    if (options.renderShot) {
                        drawnNested = options.renderShot(nested);
                        // (a document saved without its own picture cannot tell)
                        const double difference = inner->hasCompositeImage()
                            ? blockDifference(inner->compositeImage(), drawnNested) : -1.0;
                        if (difference > kMaxSplitDifference) {
                            nested.reset();
                            splitRejected = true;
                            drawnNested = QImage();
                            context.result->nestedShots.resize(shotsBefore);
                            while (notes->size() > notesBefore) notes->removeLast();
                            context.result->layerCount = layersBefore;
                            addNote(notes, PsdImportText::tr("%1: its source document is not split into layers: split, "
                                                             "it would differ from Photoshop's picture of it by %2 levels.")
                                               .arg(record.name).arg(difference, 0, 'f', 1));
                        }
                    }
                }
                // Camera Raw beyond what converts: a table fitted to what
                // Photoshop made of the source - of the source as this port
                // draws it when it can, so that the table also takes up what
                // the split document draws differently.
                std::optional<composition::Effect> vignette;
                if (!complete && placed && onlyCameraRaw(smart)) {
                    // Its post-crop vignette, on a layer of the document's
                    // frame: when nothing is below the smart object and it
                    // covers the frame, a Grade layer right above it darkens
                    // only it, as Photoshop does. Vignette Exposure works the
                    // way Camera Raw does, centre untouched and midpoint
                    // alike; its amount runs about twice as strong (fitted on
                    // project_4's document).
                    double amount = 0.0, midpoint = 50.0;
                    for (const QVariant& entry : smart.value(QStringLiteral("filterFX")).toMap()
                                                     .value(QStringLiteral("filterFXList")).toList()) {
                        const QVariantMap filter = entry.toMap().value(QStringLiteral("Fltr")).toMap();
                        amount += valueOf(filter, "PCVA");
                        if (filter.contains(QStringLiteral("PCVM"))) midpoint = valueOf(filter, "PCVM", 50.0);
                    }
                    bool bottom = layers.isEmpty();
                    const QRect frameRect(QPoint(0, 0), document.size());
                    if (!qFuzzyIsNull(amount) && bottom && record.rect.contains(frameRect)) {
                        vignette = makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.VignetteExposure"),
                                              PsdImportText::tr("Camera Raw Vignette"),
                                              {{QStringLiteral("amount"), number(amount * 0.55)},
                                               {QStringLiteral("midpoint"), number(midpoint)}}, notes);
                    }
                    QImage sourceImage;
                    if (nested) sourceImage = drawnNested;
                    if (sourceImage.size() != sourceSize) sourceImage = media::readStillImage(sourcePath);
                    const auto toDocument = smartObjectTransform(smart, sourceSize);
                    // With the vignette apart, the table learns from the
                    // middle of the frame, which the vignette leaves alone.
                    const auto lut = toDocument ? fittedLut(sourceImage, *toDocument, document.layerImage(i),
                                                            record.rect.topLeft(), QRectF(frameRect),
                                                            vignette ? 0.8 : 0.0)
                                                : std::nullopt;
                    const QString path = lut ? writeCubeLut(*lut, context.lookDirectory,
                                                            QStringLiteral("%1 - %2 - Camera Raw").arg(name, record.name))
                                             : QString();
                    const auto made = path.isEmpty() ? std::nullopt
                        : makeEffect(specOf, QStringLiteral("com.FXHOME.HitFilm.LUT"), PsdImportText::tr("Camera Raw"),
                                     {{QStringLiteral("lutFilePath"), path}, {QStringLiteral("strength"), QStringLiteral("100")}},
                                     notes);
                    if (made) {
                        filters = {*made};
                        complete = true;
                        filterNotes.clear();
                        addNote(notes, vignette
                            ? PsdImportText::tr("%1: Camera Raw is carried over as a look-up table fitted to Photoshop's "
                                                "result and its vignette as a Grade layer above it; its grain and local "
                                                "contrast are averaged into the table.").arg(record.name)
                            : PsdImportText::tr("%1: Camera Raw is carried over as a look-up table fitted to "
                                                "Photoshop's result; its grain, vignette and local contrast are "
                                                "averaged into it.").arg(record.name));
                    } else {
                        vignette.reset();
                    }
                }
                for (const QString& note : filterNotes) addNote(notes, note);
                // The source drawn here when it is split, or when there are
                // filters to put on it; a document that is not split shows
                // Photoshop's own pixels of the layer.
                if (complete && placed && (nested || hasFilters)) {
                    fromSource = true;
                    pendingVignette = vignette;
                    if (maskShape) applyMaskShape(layer, *maskShape);
                    else applyPictureMask(layer, document, documentPath, i, base);
                    layer.transform.position = placement.position;
                    layer.transform.scalePercent = placement.scalePercent;
                    layer.transform.rotationDegrees = placement.rotationDegrees;
                    if (nested) {
                        context.result->nestedShots.append(nested);
                        composition::Clip clip;
                        clip.startSeconds = 0.0;
                        clip.durationSeconds = options.durationSeconds;
                        clip.mediaId = core::Identifier(QStringLiteral("composition:") + nested->id().value());
                        clip.nestedCompositionId = nested->id();
                        clip.nestedComposition = nested;
                        layer.clips.append(clip);
                    } else {
                        layer.clips.append(clipOf(sourcePath));
                    }
                    effects = filters;
                } else if (hasFilters || (inner && !splitRejected)) {
                    QString reason;
                    if (!embedded)
                        reason = PsdImportText::tr("its source is not embedded in the document");
                    else if (!sourceSize.isValid())
                        reason = PsdImportText::tr("its source (%1) cannot be read here")
                                     .arg(document.linkedFiles().value(uid).name);
                    else if (!placed)
                        reason = PsdImportText::tr("it is warped or in perspective");
                    else
                        reason = PsdImportText::tr("not every filter could be converted");
                    addNote(notes, hasFilters
                        ? PsdImportText::tr("%1: its smart filters stay applied to its pixels (%2).").arg(record.name, reason)
                        : PsdImportText::tr("%1: its source document is not split into layers (%2).").arg(record.name, reason));
                }
            }
        }
        if (!fromSource) {
            QString assetPath = documentPath + QLatin1String(media::kPsdLayerMarker) + QString::number(record.id);
            if (base >= 0)
                assetPath += QLatin1String(media::kPsdClipMarker) + QString::number(records.at(base).id);
            else if (record.clipping)
                addNote(notes, PsdImportText::tr("%1: it is clipped to a layer that cannot be found; it is shown unclipped.")
                                   .arg(record.name));
            placeAtRect(layer, record.rect);
            layer.clips.append(clipOf(assetPath));
        }
        if (!layer.clips.first().mediaId.isValid()) {
            addNote(notes, PsdImportText::tr("%1: its pixels could not be read.").arg(record.name));
            continue;
        }
        const QVector<composition::Effect> style = psdLayerStyleEffects(
            record.effects, document.globalLightAngle(), QSizeF(record.rect.size()), specOf, record.name, notes);
        // Fill opacity fades the pixels but not the effects in Photoshop;
        // here the layer's opacity fades both.
        if (record.fillOpacity < 255) {
            layer.opacity *= record.fillOpacity / 255.0;
            if (!style.isEmpty() || !effects.isEmpty())
                addNote(notes, PsdImportText::tr("%1: its fill opacity applies to its effects too.").arg(record.name));
        }
        layer.clips.first().effects = effects;
        layerOfRecord.insert(i, int(layers.size()));
        layers.append(layer);
        styles.append(style);
        ++context.result->layerCount;
        if (pendingVignette) {
            composition::Layer grade;
            grade.name = PsdImportText::tr("%1 - Vignette").arg(record.name);
            grade.kind = composition::LayerKind::Grade;
            grade.locked = locked;
            composition::Clip clip = clipOf(QString());
            clip.effects.append(*pendingVignette);
            grade.clips.append(clip);
            layers.append(grade);
            styles.append(QVector<composition::Effect>());
            pendingVignette.reset();
        }
    }
    for (int k = 0; k < layers.size(); ++k) layers[k].clips.first().effects += styles.at(k);
    std::reverse(layers.begin(), layers.end());
    shot->setLayers(layers);
    return shot;
}

} // namespace

PsdImportResult importPsdComposition(const QString& path, media::MediaManager& media,
                                     const EffectSpecLookup& specOf, const PsdImportOptions& options)
{
    PsdImportResult result;
    const QString file = QFileInfo(path).absoluteFilePath();
    const auto document = media::openPsdDocument(file);
    if (!document) {
        media::PsdDocument probe;
        probe.load(file);
        result.error = probe.errorString().isEmpty() ? QStringLiteral("The Photoshop document cannot be read.")
                                                     : probe.errorString();
        return result;
    }
    ImportContext context{media, specOf, options,
                          options.lookDirectory.isEmpty() ? defaultLookDirectory() : options.lookDirectory, &result};
    result.shot = importDocument(*document, file, QFileInfo(path).completeBaseName(), 0, context);
    return result;
}
} // namespace project
} // namespace openvegas
