#include "project/PsdImport.h"

#include "media/MediaManager.h"
#include "media/PsdDocument.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QLineF>
#include <QRegularExpression>
#include <QSet>
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
                for (const auto& [key, name] : {std::pair{"Clarity2012", "Clarity"}, std::pair{"Texture", "Texture"},
                                                std::pair{"Temperature", "Temperature"}, std::pair{"Tint", "Tint"}}) {
                    if (!qFuzzyIsNull(raw.value(QLatin1String(key))) && !unconverted.contains(translated(name)))
                        unconverted.append(translated(name));
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

} // namespace

PsdImportResult importPsdComposition(const QString& path, media::MediaManager& media,
                                     const EffectSpecLookup& specOf, const PsdImportOptions& options)
{
    PsdImportResult result;
    media::PsdDocument document;
    if (!document.load(path)) {
        result.error = document.errorString();
        return result;
    }
    const QString file = QFileInfo(path).absoluteFilePath();
    auto shot = std::make_shared<composition::Composition>();
    shot->setName(QFileInfo(path).completeBaseName());
    shot->setSize(document.size().width(), document.size().height());
    shot->setFrameRate(options.fpsNumerator, qMax(1, options.fpsDenominator));
    shot->setDurationSeconds(options.durationSeconds);
    shot->setAudioSampleRate(options.audioSampleRate);
    QStringList* notes = &result.notes;

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

    QVector<composition::Layer> layers;
    for (int i = 0; i < records.size(); ++i) {
        const media::PsdLayer& record = records.at(i);
        if (record.kind == media::PsdLayer::Kind::GroupStart || record.kind == media::PsdLayer::Kind::GroupEnd)
            continue;
        composition::Layer layer;
        layer.name = record.name;
        layer.visible = shown.at(i);
        layer.opacity = record.opacity / 255.0 * groupOpacity.at(i);
        bool exact = true;
        layer.blendMode = psdBlendModeName(record.blendKey, &exact);
        if (!exact)
            addNote(notes, PsdImportText::tr("%1: the blend mode \"%2\" is shown as \"%3\".")
                               .arg(record.name, photoshopModeName(record.blendKey),
                                    composition::blendModeDisplayName(layer.blendMode)));

        if (record.kind == media::PsdLayer::Kind::Adjustment) {
            layer.kind = composition::LayerKind::Grade;
            composition::Clip clip = clipOf(QString());
            if (const auto effect = adjustmentEffect(record, specOf, notes)) clip.effects.append(*effect);
            if (clip.effects.isEmpty()) continue;
            if (record.clipping)
                addNote(notes, PsdImportText::tr("%1: a clipped adjustment applies to everything below it.").arg(record.name));
            if (record.hasRasterMask || record.hasVectorMask)
                addNote(notes, PsdImportText::tr("%1: the adjustment's mask is not kept.").arg(record.name));
            layer.clips.append(clip);
            layers.append(layer);
            ++result.layerCount;
            continue;
        }
        if (record.rect.isEmpty()) continue;       // an empty layer

        // The base a clipped layer is clipped to: the nearest unclipped
        // layer below it in its group.
        int base = -1;
        if (record.clipping) {
            for (int b = i - 1; b >= 0; --b) {
                const media::PsdLayer& below = records.at(b);
                if (below.kind == media::PsdLayer::Kind::GroupStart || below.kind == media::PsdLayer::Kind::GroupEnd)
                    break;
                if (below.clipping) continue;
                if (below.kind != media::PsdLayer::Kind::Adjustment && !below.rect.isEmpty()) base = b;
                break;
            }
        }
        layer.kind = composition::LayerKind::Media;
        QVector<composition::Effect> effects;

        // A smart object with smart filters that all have an equivalent here,
        // whose source is embedded and readable, placed without warp or
        // masks: the unfiltered source with the filters as effects, editable.
        bool fromSource = false;
        if (record.kind == media::PsdLayer::Kind::SmartObject
            && !record.smartObject.value(QStringLiteral("filterFX")).toMap().isEmpty()) {
            bool complete = false;
            QStringList filterNotes;
            const QVector<composition::Effect> filters =
                psdSmartFilterEffects(record.smartObject, specOf, record.name, &complete, &filterNotes);
            const QString uid = record.smartObject.value(QStringLiteral("Idnt")).toString();
            const QString sourcePath = file + QLatin1String(media::kPsdSourceMarker) + uid;
            const QSize sourceSize = document.linkedFiles().contains(uid) ? media::stillImageSize(sourcePath) : QSize();
            composition::LayerTransform placement;
            const bool placed = sourceSize.isValid()
                && smartObjectPlacement(record.smartObject, sourceSize, document.size(), &placement);
            for (const QString& note : filterNotes) addNote(notes, note);
            if (complete && placed && !record.hasRasterMask && !record.hasVectorMask && base < 0) {
                fromSource = true;
                layer.transform.position = placement.position;
                layer.transform.scalePercent = placement.scalePercent;
                layer.transform.rotationDegrees = placement.rotationDegrees;
                layer.clips.append(clipOf(sourcePath));
                effects = filters;
            } else {
                QString reason;
                if (!document.linkedFiles().contains(uid))
                    reason = PsdImportText::tr("its source is not embedded in the document");
                else if (!sourceSize.isValid())
                    reason = PsdImportText::tr("its source (%1) cannot be read here")
                                 .arg(document.linkedFiles().value(uid).name);
                else if (!placed)
                    reason = PsdImportText::tr("it is warped or in perspective");
                else if (record.hasRasterMask || record.hasVectorMask || base >= 0)
                    reason = PsdImportText::tr("it is masked or clipped");
                else
                    reason = PsdImportText::tr("not every filter could be converted");
                addNote(notes, PsdImportText::tr("%1: its smart filters stay applied to its pixels (%2).").arg(record.name, reason));
            }
        }
        if (!fromSource) {
            QString assetPath = file + QLatin1String(media::kPsdLayerMarker) + QString::number(record.id);
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
        effects += psdLayerStyleEffects(record.effects, document.globalLightAngle(), QSizeF(record.rect.size()),
                                        specOf, record.name, notes);
        // Fill opacity fades the pixels but not the effects in Photoshop;
        // here the layer's opacity fades both.
        if (record.fillOpacity < 255) {
            layer.opacity *= record.fillOpacity / 255.0;
            if (!effects.isEmpty())
                addNote(notes, PsdImportText::tr("%1: its fill opacity applies to its effects too.").arg(record.name));
        }
        layer.clips.first().effects = effects;
        layers.append(layer);
        ++result.layerCount;
    }
    std::reverse(layers.begin(), layers.end());
    shot->setLayers(layers);
    result.shot = shot;
    return result;
}

} // namespace project
} // namespace openvegas
