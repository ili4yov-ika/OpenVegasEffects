#include "project/EffectPresets.h"

#include "project/NativeEffectCatalog.h"
#include "project/VegfxSerializer.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <optional>

namespace openvegas {
namespace project {
namespace {

bool flag(const QDomElement& element, const char* name)
{
    return element.attribute(QString::fromLatin1(name)).trimmed().toInt() != 0;
}

bool readDocument(const QString& path, QDomDocument* document, int* result)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (result) *result = PresetCannotOpen;
        return false;
    }
    if (!document->setContent(&file) || document->documentElement().tagName() != QLatin1String("Preset")) {
        if (result) *result = PresetCannotRead;
        return false;
    }
    const QDomElement root = document->documentElement();
    if (root.attribute(QStringLiteral("Version"), QStringLiteral("0")).toInt() > kEffectPresetVersion) {
        if (result) *result = PresetTooNew;
        return false;
    }
    if (result) *result = PresetOk;
    return true;
}

} // namespace

QString effectPresetError(int code)
{
    // The reference's strings, translated in the context its QObject::tr uses.
    switch (code) {
    case PresetOk: return QString();
    case PresetCannotOpen:
        return QCoreApplication::translate("QObject", "Unable to open preset file.");
    case PresetCannotWrite:
        return QCoreApplication::translate("QObject", "Unable to write preset file.");
    case PresetCannotRead:
        return QCoreApplication::translate("QObject", "Unable to read content from preset file.");
    case PresetNoPath:
        return QCoreApplication::translate("QObject",
                                           "Unable to retrieve the choosen path, the preset will not be saved.");
    case PresetTooNew:
        return QCoreApplication::translate(
                   "QObject", "This preset cannot be used. It may have been saved using a newer version of %1.")
            .arg(QCoreApplication::applicationName());
    default: break;
    }
    return QCoreApplication::translate("QObject", "Error during preset creation.");
}

bool readEffectPreset(const QString& path, EffectPreset* preset, int* result)
{
    QDomDocument document;
    if (!readDocument(path, &document, result)) return false;
    const QDomElement root = document.documentElement();
    if (preset) {
        preset->path = QFileInfo(path).absoluteFilePath();
        // Shown under its file name, translated in the "EffectPreset" context
        // (FUN_140676c00) as the shipped presets are.
        const QByteArray name = QFileInfo(path).completeBaseName().toUtf8();
        preset->name = QCoreApplication::translate("EffectPreset", name.constData());
        preset->id = root.firstChildElement(QStringLiteral("ID")).text().trimmed();
        preset->threeD = root.attribute(QStringLiteral("Type")) == QLatin1String("3D");
        preset->editable = !root.hasAttribute(QStringLiteral("Editable")) || flag(root, "Editable");
        preset->hasVisualEffects = flag(root, "HasVisualEffects");
        preset->hasAudioEffects = flag(root, "HasAudioEffects");
        preset->hasGeometryEffects = flag(root, "HasGeometryEffects");
        preset->hasBehaviorEffects = flag(root, "HasBehaviorEffects");
        preset->compositeShotOnly = flag(root, "HasCompositeShotOnly");
    }
    return true;
}

QVector<composition::Effect> effectPresetEffects(const QString& path, double fps, int* result)
{
    QDomDocument document;
    if (!readDocument(path, &document, result)) return {};
    const QDomElement root = document.documentElement();
    if (root.attribute(QStringLiteral("Type")) == QLatin1String("3D")) {
        if (result) *result = PresetCannotRead;
        return {};
    }
    QVector<composition::Effect> effects =
        VegfxSerializer::effectsFromInstances(root.firstChildElement(QStringLiteral("EffectsPreset")), fps);
    // Each use makes instances of its own.
    for (composition::Effect& effect : effects) effect.instanceId = core::Identifier();
    if (effects.isEmpty() && result) *result = PresetCannotRead;
    return effects;
}

int writeEffectPreset(const QString& path, const QVector<composition::Effect>& effects, double fps)
{
    if (path.trimmed().isEmpty()) return PresetNoPath;
    if (!QFileInfo(path).absoluteDir().exists()) return PresetCannotWrite;
    QDomDocument document;
    document.appendChild(document.createProcessingInstruction(QStringLiteral("xml"),
                                                              QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = document.createElement(QStringLiteral("Preset"));
    root.setAttribute(QStringLiteral("Type"), QStringLiteral("2D"));
    root.setAttribute(QStringLiteral("Version"), kEffectPresetVersion);
    root.setAttribute(QStringLiteral("Editable"), 1);
    document.appendChild(root);
    QDomElement id = document.createElement(QStringLiteral("ID"));
    id.appendChild(document.createTextNode(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    root.appendChild(id);
    QDomElement list = document.createElement(QStringLiteral("EffectsPreset"));
    root.appendChild(list);
    // The kinds it holds, by the plugin of each (AbstractPlugin::Type).
    bool visual = false, audio = false, geometry = false, behavior = false;
    for (const composition::Effect& effect : effects) {
        const std::optional<NativeEffectEntry> entry = findNativeEffect(effect.pluginId.value());
        if (!entry) continue;
        switch (entry->container) {
        case NativeEffectEntry::Container::Effects: (entry->audio ? audio : visual) = true; break;
        case NativeEffectEntry::Container::GeometryEffects: geometry = true; break;
        case NativeEffectEntry::Container::BehaviorEffects: behavior = true; break;
        case NativeEffectEntry::Container::None: break;
        }
    }
    VegfxSerializer::appendEffectInstances(document, list, effects, fps);
    if (list.firstChildElement(QStringLiteral("Instance")).isNull()) return PresetNothingToSave;
    root.setAttribute(QStringLiteral("HasVisualEffects"), visual ? 1 : 0);
    root.setAttribute(QStringLiteral("HasAudioEffects"), audio ? 1 : 0);
    root.setAttribute(QStringLiteral("HasGeometryEffects"), geometry ? 1 : 0);
    root.setAttribute(QStringLiteral("HasBehaviorEffects"), behavior ? 1 : 0);
    root.setAttribute(QStringLiteral("HasCompositeShotOnly"), 0);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return PresetCannotWrite;
    const QByteArray bytes = document.toByteArray(4);
    if (file.write(bytes) != bytes.size() || !file.commit()) return PresetCannotWrite;
    return PresetOk;
}

QVector<EffectPreset> scanEffectPresets(const QString& directory)
{
    QVector<EffectPreset> presets;
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) return presets;
    QDirIterator it(directory, {QStringLiteral("*.hfpr")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        EffectPreset preset;
        if (readEffectPreset(it.next(), &preset)) presets.append(preset);
    }
    std::sort(presets.begin(), presets.end(), [](const EffectPreset& a, const EffectPreset& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return presets;
}

QString uniqueFilePath(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) return path;
    const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    const QString suffix = info.suffix();
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
        if (!QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    }
}

} // namespace project
} // namespace openvegas
