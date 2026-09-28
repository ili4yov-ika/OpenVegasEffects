#include "plugin/PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <memory>

#include "core/Log.h"
#include "core/Result.h"
#include "plugin/EffectRender.h"
#include "plugin/NativeEffectRender.h"
#include "plugin/NativePlugin.h"

namespace openvegas {
namespace plugin {

QString EffectSpec::kindName() const
{
    switch (kind) {
    case PluginKind::Effects:      return QStringLiteral("Effects");
    case PluginKind::ColorEffects: return QStringLiteral("Color");
    case PluginKind::Transitions:  return QStringLiteral("Transitions");
    case PluginKind::Generators:   return QStringLiteral("Generators");
    case PluginKind::Filters:      return QStringLiteral("Filters");
    case PluginKind::Emission:     return QStringLiteral("Emission");
    case PluginKind::AssetImporter: return QStringLiteral("Import");
    case PluginKind::Exporter:     return QStringLiteral("Export");
    case PluginKind::Effect2D:        return QStringLiteral("2D");
    case PluginKind::EffectAE2D:      return QStringLiteral("After Effects");
    case PluginKind::AudioEffect:     return QStringLiteral("Audio");
    case PluginKind::AudioTransition: return QStringLiteral("Audio Transitions");
    case PluginKind::BehaviorEffect:  return QStringLiteral("Behavior");
    case PluginKind::GeometryEffect:  return QStringLiteral("Geometry");
    case PluginKind::VideoTransition: return QStringLiteral("Video Transitions");
    case PluginKind::Unknown:      return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

namespace {

struct PluginEntry
{
    std::shared_ptr<Plugin> plugin;
    EffectSpec spec;
};

QVector<EffectParameterSpec> recoveredNativeParameters(const QString& baseName)
{
    if (baseName.compare(QStringLiteral("BrightnessContrast"),
                         Qt::CaseInsensitive) == 0) {
        const auto scalar = [](const QString& name, const QString& label) {
            EffectParameterSpec parameter;
            parameter.name = name;
            parameter.displayName = label;
            parameter.type = QStringLiteral("double");
            parameter.defaultValue = QStringLiteral("0");
            parameter.unit = QStringLiteral("%");
            parameter.minimum = -100.0;
            parameter.maximum = 100.0;
            parameter.decimals = 1;
            parameter.step = 1.0;
            return parameter;
        };
        return {scalar(QStringLiteral("brightness"), QStringLiteral("Brightness")),
                scalar(QStringLiteral("contrast"), QStringLiteral("Contrast"))};
    }
    if (baseName.compare(QStringLiteral("Gamma"), Qt::CaseInsensitive) == 0) {
        const auto channel = [](const QString& name, const QString& label) {
            EffectParameterSpec parameter;
            parameter.name = name;
            parameter.displayName = label;
            parameter.type = QStringLiteral("double");
            parameter.defaultValue = QStringLiteral("1");
            parameter.minimum = 0.1;
            parameter.maximum = 5.0;
            parameter.decimals = 2;
            parameter.step = 0.1;
            return parameter;
        };
        return {channel(QStringLiteral("redGamma"), QStringLiteral("Red Gamma")),
                channel(QStringLiteral("greenGamma"), QStringLiteral("Green Gamma")),
                channel(QStringLiteral("blueGamma"), QStringLiteral("Blue Gamma"))};
    }
    if (baseName.compare(QStringLiteral("Fill"), Qt::CaseInsensitive) == 0) {
        EffectParameterSpec color;
        color.name = QStringLiteral("fillColor");
        color.displayName = QStringLiteral("Color");
        color.type = QStringLiteral("color");
        color.defaultValue = QStringLiteral("#ffffff");

        EffectParameterSpec amount;
        amount.name = QStringLiteral("blendAmount");
        amount.displayName = QStringLiteral("Blend Amount");
        amount.type = QStringLiteral("double");
        amount.defaultValue = QStringLiteral("100");
        amount.unit = QStringLiteral("%");
        amount.minimum = 0.0;
        amount.maximum = 100.0;
        amount.decimals = 1;
        amount.step = 1.0;
        return {color, amount};
    }
    if (baseName.compare(QStringLiteral("ColorTemperature"),
                         Qt::CaseInsensitive) == 0) {
        EffectParameterSpec temperature;
        temperature.name = QStringLiteral("temperatureShift");
        temperature.displayName = QStringLiteral("Temperature");
        temperature.type = QStringLiteral("double");
        temperature.defaultValue = QStringLiteral("4900");
        temperature.unit = QStringLiteral("K");
        temperature.minimum = 1500.0;
        temperature.maximum = 13500.0;
        temperature.decimals = 0;
        temperature.step = 100.0;
        return {temperature};
    }
    if (baseName.compare(QStringLiteral("CrushBlacksWhites"),
                         Qt::CaseInsensitive) == 0) {
        const auto level = [](const QString& name, const QString& label,
                              const QString& defaultValue) {
            EffectParameterSpec parameter;
            parameter.name = name;
            parameter.displayName = label;
            parameter.type = QStringLiteral("double");
            parameter.defaultValue = defaultValue;
            parameter.minimum = 0.0;
            parameter.maximum = 1.0;
            parameter.decimals = 2;
            parameter.step = 0.01;
            return parameter;
        };
        return {level(QStringLiteral("inputBlack"), QStringLiteral("Black"),
                      QStringLiteral("0")),
                level(QStringLiteral("inputWhite"), QStringLiteral("White"),
                      QStringLiteral("1"))};
    }
    if (baseName.compare(QStringLiteral("FindEdges"), Qt::CaseInsensitive) == 0) {
        EffectParameterSpec inverted;
        inverted.name = QStringLiteral("isInverted");
        inverted.displayName = QStringLiteral("Invert");
        inverted.type = QStringLiteral("bool");
        inverted.defaultValue = QStringLiteral("false");
        return {inverted};
    }
    if (baseName.compare(QStringLiteral("Threshold"), Qt::CaseInsensitive) != 0) {
        return {};
    }

    // Threshold.hfpl registers these four controls in its setup routine. The
    // scalar constants are 1/50/100/1 (minimum/default/maximum/step); the
    // source choice vector contains the six adjacent UTF-16 labels and uses
    // index 4 (Lightness) as its default.
    EffectParameterSpec threshold;
    threshold.name = QStringLiteral("threshold");
    threshold.displayName = QStringLiteral("Threshold");
    threshold.type = QStringLiteral("double");
    threshold.defaultValue = QStringLiteral("50");
    threshold.unit = QStringLiteral("%");
    threshold.minimum = 1.0;
    threshold.maximum = 100.0;
    threshold.decimals = 1;
    threshold.step = 1.0;

    EffectParameterSpec color1;
    color1.name = QStringLiteral("color1");
    color1.displayName = QStringLiteral("Color 1");
    color1.type = QStringLiteral("color");
    color1.defaultValue = QStringLiteral("#ffffff");

    EffectParameterSpec color2;
    color2.name = QStringLiteral("color2");
    color2.displayName = QStringLiteral("Color 2");
    color2.type = QStringLiteral("color");
    color2.defaultValue = QStringLiteral("#000000");

    EffectParameterSpec source;
    source.name = QStringLiteral("source");
    source.displayName = QStringLiteral("Source");
    source.type = QStringLiteral("enum");
    source.choices = {QStringLiteral("Red"), QStringLiteral("Green"),
                      QStringLiteral("Blue"), QStringLiteral("Luminance"),
                      QStringLiteral("Lightness"), QStringLiteral("Average")};
    source.defaultValue = source.choices.at(4);

    return {threshold, color1, color2, source};
}

} // namespace

class LocalPlugin : public Plugin
{
public:
    LocalPlugin(QString path, EffectSpec spec)
        : m_path(std::move(path))
        , m_spec(std::move(spec))
    {
    }

    PluginId id() const override { return m_spec.id; }
    QString name() const override { return m_spec.name; }
    QString category() const override { return m_spec.category; }
    core::Version version() const override { return m_spec.version; }
    QString libraryPath() const override { return m_path; }
    PluginKind kind() const override { return m_spec.kind; }

    const EffectSpec& specRef() const { return m_spec; }

private:
    QString m_path;
    EffectSpec m_spec;
};

class LocalEffectInstance : public EffectInstance
{
public:
    explicit LocalEffectInstance(EffectSpec spec)
        : m_spec(std::move(spec))
    {
        for (const EffectParameterSpec& p : m_spec.parameters) {
            m_values.insert(p.name, p.defaultValue);
        }
    }

    EffectSpec spec() const override { return m_spec; }

    void setParameter(const QString& name, const QString& value) override
    {
        m_values[name] = value;
    }

    QString parameter(const QString& name) const override
    {
        return m_values.value(name);
    }

    bool render(int width, int height, unsigned char* rgba) override
    {
        Q_UNUSED(width);
        Q_UNUSED(height);
        Q_UNUSED(rgba);
        return true;
    }

private:
    EffectSpec m_spec;
    QHash<QString, QString> m_values;
};

class DefaultPluginManager : public PluginManager
{
public:
    void setPluginDirs(const QVector<QString>& dirs) override
    {
        m_dirs = dirs;
    }

    void setCallbacks(const Callbacks& callbacks) override
    {
        m_callbacks = callbacks;
    }

    core::Result scan() override
    {
        m_entries.clear();
        clearNativeEffectModules();
        registerBuiltins();
        loadMetadataCache();
        m_cacheDirty = false;

        for (const QString& dirPath : m_dirs) {
            QDir dir(dirPath);
            if (!dir.exists()) {
                OV_LOG_WARN(QStringLiteral("Plugin directory does not exist: %1").arg(dirPath));
                continue;
            }

            // Four of the reference's own plugins link opencv_world460.dll and
            // one more module from the application folder. Those sit beside the
            // "Plugins" directory, not inside it, so the folder above the scan
            // root goes on the module search path - otherwise LoadLibrary fails
            // for a reason that has nothing to do with the plugin.
            const QString dependencyDir = QFileInfo(dir.absolutePath()).absolutePath();
            scanDirectory(dir, 0, dependencyDir);
        }
        saveMetadataCache();
        return core::Result::ok();
    }

    QVector<PluginId> allPluginIds() const override
    {
        QVector<PluginId> ids;
        ids.reserve(m_entries.size());
        for (const auto& entry : m_entries) {
            ids.push_back(entry.spec.id);
        }
        return ids;
    }

    QVector<PluginId> pluginIdsByKind(PluginKind kind) const override
    {
        QVector<PluginId> ids;
        for (const auto& entry : m_entries) {
            if (entry.spec.kind == kind) {
                ids.push_back(entry.spec.id);
            }
        }
        return ids;
    }

    QVector<PluginId> pluginIdsByCategory(const QString& category) const override
    {
        QVector<PluginId> ids;
        for (const auto& entry : m_entries) {
            if (entry.spec.category == category) {
                ids.push_back(entry.spec.id);
            }
        }
        return ids;
    }

    std::shared_ptr<Plugin> plugin(const PluginId& id) const override
    {
        for (const auto& entry : m_entries) {
            if (entry.spec.id == id) {
                return entry.plugin;
            }
        }
        return nullptr;
    }

    EffectSpec spec(const PluginId& id) const override
    {
        for (const auto& entry : m_entries) {
            if (entry.spec.id == id) {
                return entry.spec;
            }
        }
        return EffectSpec{};
    }

    std::shared_ptr<EffectInstance> createEffect(const PluginId& id) override
    {
        const EffectSpec s = spec(id);
        if (s.id.isValid()) {
            return std::make_shared<LocalEffectInstance>(s);
        }
        return nullptr;
    }

private:
    void addBuiltin(EffectSpec spec, const QString& category)
    {
        spec.category = category;
        spec.kind = PluginKind::Effects;
        PluginEntry entry;
        entry.plugin = std::make_shared<LocalPlugin>(QString(), spec);
        entry.spec = std::move(spec);
        m_entries.push_back(entry);
    }

    void registerBuiltins()
    {
        for (auto spec : timelineBuiltinSpecs()) { const QString category = spec.category; addBuiltin(std::move(spec), category); }
        {
            EffectSpec spec;
            spec.id = PluginId(QLatin1String(kBuiltinGrayscale));
            spec.name = QStringLiteral("grayscale");
            spec.displayName = QStringLiteral("Grayscale");
            spec.version = core::Version(1, 0, 0);
            addBuiltin(std::move(spec), QStringLiteral("Color"));
        }
        {
            EffectSpec spec;
            spec.id = PluginId(QLatin1String(kBuiltinSepia));
            spec.name = QStringLiteral("sepia");
            spec.displayName = QStringLiteral("Sepia");
            spec.version = core::Version(1, 0, 0);
            addBuiltin(std::move(spec), QStringLiteral("Color"));
        }
        {
            EffectSpec spec;
            spec.id = PluginId(QLatin1String(kBuiltinInvert));
            spec.name = QStringLiteral("invert");
            spec.displayName = QStringLiteral("Invert");
            spec.version = core::Version(1, 0, 0);
            addBuiltin(std::move(spec), QStringLiteral("Color"));
        }
        {
            EffectParameterSpec p;
            p.name = QStringLiteral("tint");
            p.displayName = QStringLiteral("Tint Color");
            p.type = QStringLiteral("color");
            p.defaultValue = QStringLiteral("#00ff00");
            EffectSpec spec;
            spec.id = PluginId(QLatin1String(kBuiltinTint));
            spec.name = QStringLiteral("tint");
            spec.displayName = QStringLiteral("Tint");
            spec.description = QStringLiteral("Tint: multiply luminance by a color.");
            spec.version = core::Version(1, 0, 0);
            spec.parameters.push_back(p);
            addBuiltin(std::move(spec), QStringLiteral("Color"));
        }
        {
            EffectParameterSpec p;
            p.name = QStringLiteral("level");
            p.displayName = QStringLiteral("Level");
            p.unit = QStringLiteral("%");
            p.type = QStringLiteral("int");
            p.defaultValue = QStringLiteral("100");
            EffectSpec spec;
            spec.id = PluginId(QLatin1String(kBuiltinBrightness));
            spec.name = QStringLiteral("brightness");
            spec.displayName = QStringLiteral("Brightness");
            spec.version = core::Version(1, 0, 0);
            spec.parameters.push_back(p);
            addBuiltin(std::move(spec), QStringLiteral("Color"));
        }
        {
            // The text effect is drawn by the renderer itself (RenderWorker
            // looks for this id), but it still needs a spec: without one the
            // Controls panel and the timeline tree had no parameter names and
            // fell back to "Parameter 1" / "Parameter 2" on every text layer.
            //
            // The order is composition::TextStyle's, because that is what reads
            // and writes these values, and the names and labels are the ones the
            // reference's Text panel uses for the same controls.
            struct TextParam
            {
                const char* name;
                const char* label;
                const char* type;
                const char* unit;
                const char* defaultValue;
            };
            static const TextParam textParams[] = {
                {"text", "Text", "multiline", "", ""},
                {"fontSize", "Font Size", "int", "px", "48"},
                {"fontFamily", "Font", "string", "", ""},
                {"fontStyle", "Font Style", "string", "", "Regular"},
                {"fontColor", "Font Color", "color", "", "#ffffff"},
                {"outlineSize", "Outline", "double", "px", "0.00"},
                {"outlineColor", "Outline Color", "color", "", "#000000"},
                {"strokeOrder", "Stroke Order", "string", "", "Over Fill"},
                {"lineSpacing", "Line Spacing", "double", "%", "100.00"},
                {"verticalScale", "Vertical Scale", "double", "%", "100.00"},
                {"horizontalScale", "Horizontal Scale", "double", "%", "100.00"},
                // The reference's own field reads per mille, so the unit does too.
                {"letterSpacing", "Character Spacing", "double", "\xE2\x80\xB0", "0.00"},
                {"baselineShift", "Baseline Shift", "double", "%", "0.00"},
                {"caps", "Caps", "string", "", "None"},
                {"underline", "Underline", "bool", "", "false"},
                {"strikethrough", "Strikethrough", "bool", "", "false"},
                {"script", "Script", "string", "", "None"},
                {"alignH", "Alignment", "string", "", "Center"},
                {"alignV", "Vertical Alignment", "string", "", "Middle"},
                {"indentLeft", "Left Indentation", "double", "", "0.00"},
                {"indentRight", "Right Indentation", "double", "", "0.00"},
                {"indentTop", "Top Indentation", "double", "", "0.00"},
                {"indentBottom", "Bottom Indentation", "double", "", "0.00"},
                {"indentFirstLine", "First Line Indentation", "double", "", "0.00"},
                {"spaceBefore", "Gap Before Paragraph", "double", "", "0.00"},
                {"spaceAfter", "Gap After Paragraph", "double", "", "0.00"},
                {"backgroundColor", "Background Color", "color", "", "#000000"},
                {"backgroundOpacity", "Background Opacity", "double", "%", "0.00"},
                {"backgroundRoundness", "Background Corners", "double", "%", "0.00"},
                {"backgroundExpansionX", "Background X-Axis Expansion", "double", "%", "0.00"},
                {"backgroundExpansionY", "Background Y-Axis Expansion", "double", "%", "0.00"},
                {"expansionLinked", "Link Expansion", "bool", "", "true"},
                {"additionalOutlines", "Additional Outlines", "string", "", "[]"},
                {"backgroundEnabled", "Background", "bool", "", "false"},
            };

            EffectSpec spec;
            spec.id = PluginId(QStringLiteral("text"));
            spec.name = QStringLiteral("text");
            spec.displayName = QStringLiteral("Text");
            spec.description = QStringLiteral("Draws a text box on the layer.");
            spec.version = core::Version(1, 0, 0);
            for (const TextParam& param : textParams) {
                EffectParameterSpec p;
                p.name = QString::fromLatin1(param.name);
                p.displayName = QString::fromLatin1(param.label);
                p.type = QString::fromLatin1(param.type);
                p.unit = QString::fromUtf8(param.unit);
                p.defaultValue = QString::fromLatin1(param.defaultValue);
                spec.parameters.push_back(p);
            }
            addBuiltin(std::move(spec), QStringLiteral("Text"));
        }
    }

    // The reference stores plugins in per-category sub-folders of "Plugins"
    // (2D, Audio, AudioTransitions, Behavior, Geometry, VideoTransitions), so
    // the scan has to descend rather than look at the top level only.
    static constexpr int kMaxScanDepth = 4;

    void scanDirectory(const QDir& dir, int depth, const QString& dependencyDir)
    {
        const QStringList entries =
            dir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            const QString path = dir.filePath(entry);
            const QFileInfo info(path);
            if (info.isDir()) {
                if (depth < kMaxScanDepth) {
                    scanDirectory(QDir(path), depth + 1, dependencyDir);
                }
                continue;
            }
            if (!info.isFile()) {
                continue;
            }
            const QString suffix = info.suffix().toLower();
            if (suffix == QLatin1String("vfx")) {
                registerVfxPreset(path);
            } else if (suffix == QLatin1String("hfpl") || suffix == QLatin1String("hfplx")) {
                registerNativePlugin(path, dependencyDir);
            }
        }
    }

    // Native plugins are validated the way the reference does it: the export
    // table is checked first - both entry points must be present, exactly as
    // Tannen.dll's "Missing API" guard demands - and only a file that passes
    // that gate has its PluginInfo called for the real metadata.
    //
    // Calling PluginInfo runs third-party code, which is why the static gate
    // comes first and the call itself is guarded; a plugin that faults is
    // logged and falls back to what static inspection recovered.
    // Reading a module costs around 60 ms, nearly all of it the module's own
    // DllMain, so a full set is close to twenty seconds. The reference hides
    // that behind a background thread; this port remembers the answer instead,
    // keyed on the file's size and modification time, so a rescan of unchanged
    // plugins costs nothing.
    NativePluginInfo cachedMetadata(const QString& filePath, const QString& dependencyDir)
    {
        const QFileInfo info(filePath);
        const QString key = info.absoluteFilePath();
        const QString stamp = QStringLiteral("%1/%2")
                                  .arg(info.size())
                                  .arg(info.lastModified().toMSecsSinceEpoch());

        const auto it = m_metadataCache.constFind(key);
        if (it != m_metadataCache.constEnd()
            && it->value(QStringLiteral("stamp")).toString() == stamp) {
            NativePluginInfo cached = nativePluginFromJson(*it);
            cached.filePath = filePath;
            return cached;
        }

        NativePluginInfo fresh = loadNativePluginMetadata(filePath, dependencyDir);
        // Only a reading that actually came from the module is worth keeping;
        // a failure should be retried next time, in case the missing dependency
        // has since been installed.
        if (fresh.metadataFromModule) {
            QJsonObject object = nativePluginToJson(fresh);
            object.insert(QStringLiteral("stamp"), stamp);
            m_metadataCache.insert(key, object);
            m_cacheDirty = true;
        }
        return fresh;
    }

    QString metadataCachePath() const
    {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dir.isEmpty()) {
            return QString();
        }
        return dir + QStringLiteral("/plugin-metadata.json");
    }

    void loadMetadataCache()
    {
        m_metadataCache.clear();
        const QString path = metadataCachePath();
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        // A cache written by an older build describes fields this one does not
        // know about, so it is versioned and simply dropped when it disagrees.
        if (root.value(QStringLiteral("version")).toInt() != kMetadataCacheVersion) {
            return;
        }
        const QJsonObject plugins = root.value(QStringLiteral("plugins")).toObject();
        for (auto it = plugins.constBegin(); it != plugins.constEnd(); ++it) {
            m_metadataCache.insert(it.key(), it.value().toObject());
        }
    }

    void saveMetadataCache()
    {
        if (!m_cacheDirty) {
            return;
        }
        const QString path = metadataCachePath();
        if (path.isEmpty()) {
            return;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonObject plugins;
        for (auto it = m_metadataCache.constBegin(); it != m_metadataCache.constEnd(); ++it) {
            plugins.insert(it.key(), it.value());
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"), kMetadataCacheVersion);
        root.insert(QStringLiteral("plugins"), plugins);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            OV_LOG_WARN(QStringLiteral("Unable to write the plugin metadata cache: %1").arg(path));
            return;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
    }

    void registerNativePlugin(const QString& filePath, const QString& dependencyDir)
    {
        const NativePluginInfo native = cachedMetadata(filePath, dependencyDir);
        if (!native.isValid()) {
            OV_LOG_WARN(QStringLiteral("Skipping %1: missing plugin entry point "
                                       "(PluginInfo=%2, Notify=%3)")
                            .arg(filePath)
                            .arg(native.hasPluginInfo)
                            .arg(native.hasNotify));
            return;
        }
        if (native.metadataFromModule) {
            // The reference refuses a package whose magic is wrong or whose
            // version is newer than its build: major 22 and up, or 21 with a
            // minor of 2 or more. Every shipped plugin reports 5.0.
            if (!native.magicOk) {
                OV_LOG_WARN(QStringLiteral("Skipping %1: package magic does not match")
                                .arg(filePath));
                return;
            }
            const bool tooNew = native.versionMajor >= 22
                                || (native.versionMajor == 21 && native.versionMinor >= 2);
            if (tooNew) {
                OV_LOG_WARN(QStringLiteral("Skipping %1: the plugin package information "
                                           "doesn't match the current build (%2.%3)")
                                .arg(filePath)
                                .arg(native.versionMajor)
                                .arg(native.versionMinor));
                return;
            }
            if (kindForModuleType(native.moduleType) == PluginKind::Unknown) {
                OV_LOG_WARN(QStringLiteral("Skipping %1: invalid plugin type %2")
                                .arg(filePath)
                                .arg(native.moduleType));
                return;
            }
        } else if (!native.metadataError.isEmpty()) {
            OV_LOG_WARN(QStringLiteral("Reading %1 through its PluginInfo failed (%2); "
                                       "falling back to static inspection")
                            .arg(filePath, native.metadataError));
        }

        EffectSpec spec;
        // Prefer the identifier the module declares - it is what the reference
        // keys on, and it does not always match the file name.
        // An identifier can repeat for two reasons, which need opposite
        // handling: the same module found again in a later search directory
        // (skip it, first directory wins), or two genuinely different files
        // declaring the same id - DepthMask.hfpl and DepthMatte.hfpl ship as
        // the same module under two names - which stay separate entries under a
        // file-scoped id.
        const QString fileScopedId = QStringLiteral("native:") + native.baseName;
        if (!native.identifier.isEmpty()) {
            const int existing = indexOfId(native.identifier);
            if (existing >= 0) {
                if (m_entries.at(existing).spec.name == native.baseName) {
                    return; // already registered from an earlier directory
                }
                spec.id = PluginId(fileScopedId);
            } else {
                spec.id = PluginId(native.identifier);
            }
        } else {
            if (indexOfId(fileScopedId) >= 0) {
                return;
            }
            spec.id = PluginId(fileScopedId);
        }
        spec.name = native.baseName;
        // The reference runs a plugin's own strings through Qt translation under
        // the context "Effect_<ShortName>" (PluginFile::ReadPluginMetadata), so
        // use the same context here: a catalogue written against the reference
        // naming localises our browser too. Untranslated, translate() returns the
        // source unchanged.
        const QByteArray context = native.translationContext.toUtf8();
        const QString rawName = native.displayName.isEmpty()
                                    ? displayNameForIdentifier(native.identifier, native.baseName)
                                    : native.displayName;
        spec.displayName = context.isEmpty()
                               ? rawName
                               : QCoreApplication::translate(context.constData(),
                                                             rawName.toUtf8().constData());
        // The embedded category is the one the Effects panel groups by; the
        // folder only says which wrapper class handles the plugin, so it stays
        // the fallback.
        const QString rawCategory =
            native.effectCategory.isEmpty()
                ? (native.category.isEmpty() ? QStringLiteral("Uncategorized") : native.category)
                : native.effectCategory;
        // Category names are translated in the same per-plugin context - the
        // reference stores each plugin's category string in its own catalogue
        // entry rather than in a shared one.
        spec.category = (context.isEmpty() || native.effectCategory.isEmpty())
                            ? rawCategory
                            : QCoreApplication::translate(context.constData(),
                                                          rawCategory.toUtf8().constData());
        // The type the module reports is authoritative; the folder is only the
        // fallback for a plugin whose PluginInfo could not be called.
        spec.kind = native.metadataFromModule ? kindForModuleType(native.moduleType)
                                              : kindForNativeCategory(native.category);
        spec.subCategory = native.subCategory;
        spec.keywords = native.keywords;
        spec.guid = native.guid;
        if (native.metadataFromModule) {
            spec.version = core::Version(native.versionMajor, native.versionMinor, 0);
        }
        spec.parameters = recoveredNativeParameters(native.baseName);
        if (spec.parameters.isEmpty()) {
            spec.parameters = native.parameters;
        }
        // The common 2D framebuffer path is live. Keep the capability flag to
        // modules whose complete message-8/message-10 path has a pixel-level
        // regression probe, including parameterized and multi-pass effects
        // whose value and scratch-resource services are connected.
        static const QSet<QString> verifiedNative2dRenderers {
            QStringLiteral("4pointramp"),
            QStringLiteral("360blur"),
            QStringLiteral("360channelblur"),
            QStringLiteral("360fisheyeconverter"),
            QStringLiteral("360glow"),
            QStringLiteral("360glowdarks"),
            QStringLiteral("alphabrightnesscontrast"),
            QStringLiteral("angleblur"),
            QStringLiteral("autocolor"),
            QStringLiteral("autocontrast"),
            QStringLiteral("autolevels"),
            QStringLiteral("bezierwarp"),
            QStringLiteral("bilateralblur"),
            QStringLiteral("bleachbypass"),
            QStringLiteral("boxblur"),
            QStringLiteral("brightnesscontrast"),
            QStringLiteral("bulge"),
            QStringLiteral("cartoon"),
            QStringLiteral("channelmixer"),
            QStringLiteral("channelswapper"),
            QStringLiteral("chromakey"),
            QStringLiteral("chromablur"),
            QStringLiteral("chromaticaberration"),
            QStringLiteral("cinestyle"),
            QStringLiteral("classiccinestyle"),
            QStringLiteral("colorbalance"),
            QStringLiteral("colorconverter"),
            QStringLiteral("colorcorrection"),
            QStringLiteral("colordifferencekey"),
            QStringLiteral("colorphase"),
            QStringLiteral("colortemperature"),
            QStringLiteral("colorvibrance"),
            QStringLiteral("crushblackswhites"),
            QStringLiteral("crushblackswhitesalpha"),
            QStringLiteral("crop"),
            QStringLiteral("customgray"),
            QStringLiteral("dayfornight"),
            QStringLiteral("dehaze"),
            QStringLiteral("deinterlace"),
            QStringLiteral("demult"),
            QStringLiteral("derez"),
            QStringLiteral("diffuse"),
            QStringLiteral("dotmatrix"),
            QStringLiteral("dropshadow"),
            QStringLiteral("duotone"),
            QStringLiteral("edgedistortion"),
            QStringLiteral("emboss"),
            QStringLiteral("exposure"),
            QStringLiteral("exposurepro"),
            QStringLiteral("fill"),
            QStringLiteral("findedges"),
            QStringLiteral("filmgrain"),
            QStringLiteral("fisheyewarp"),
            QStringLiteral("fisheyewarp2"),
            QStringLiteral("flyeye"),
            QStringLiteral("gamma"),
            QStringLiteral("glow"),
            QStringLiteral("glowdarks"),
            QStringLiteral("grain"),
            QStringLiteral("halftone"),
            QStringLiteral("highpasssharpen"),
            QStringLiteral("hsl"),
            QStringLiteral("huecolorize"),
            QStringLiteral("huekey"),
            QStringLiteral("hueshift"),
            QStringLiteral("invert"),
            QStringLiteral("invertalpha"),
            QStringLiteral("lensblur"),
            QStringLiteral("letterbox"),
            QStringLiteral("leavecolor"),
            QStringLiteral("levels"),
            QStringLiteral("luminancekey"),
            QStringLiteral("magnify"),
            QStringLiteral("mosaic"),
            QStringLiteral("neonglow"),
            QStringLiteral("noise"),
            QStringLiteral("oilpainting"),
            QStringLiteral("pagecurl"),
            QStringLiteral("pencilsketch"),
            QStringLiteral("perspectivewarp"),
            QStringLiteral("pip"),
            QStringLiteral("pondripple"),
            QStringLiteral("polarwarp"),
            QStringLiteral("posterize"),
            QStringLiteral("projector"),
            QStringLiteral("quadwarp"),
            QStringLiteral("radialblur"),
            QStringLiteral("radialgradient"),
            QStringLiteral("ramp"),
            QStringLiteral("reflection"),
            QStringLiteral("removestockbackground"),
            QStringLiteral("rollingshutter"),
            QStringLiteral("scanlines"),
            QStringLiteral("sharpen"),
            QStringLiteral("shadowhighlight"),
            QStringLiteral("solarize"),
            QStringLiteral("sphere"),
            QStringLiteral("spillsuppressor"),
            QStringLiteral("threshold"),
            QStringLiteral("threestripcolor"),
            QStringLiteral("tiles"),
            QStringLiteral("tint"),
            QStringLiteral("tonecoloring"),
            QStringLiteral("twirl"),
            QStringLiteral("twostripcolor"),
            QStringLiteral("unsharpen"),
            QStringLiteral("vibrance"),
            QStringLiteral("vignette"),
            QStringLiteral("vignetteexposure"),
            QStringLiteral("warpvortex"),
            QStringLiteral("waves"),
            QStringLiteral("whitebalance"),
            QStringLiteral("wireframe"),
            QStringLiteral("yuvcolorcorrection"),
            QStringLiteral("yuvcolortransform"),
            QStringLiteral("zoomblur"),
            QStringLiteral("actioncamcrop"),
            QStringLiteral("autolightflares"),
            QStringLiteral("centerwipe"),
            QStringLiteral("clone"),
            QStringLiteral("displacementmap"),
            QStringLiteral("fluiddistortion"),
            QStringLiteral("grid"),
            QStringLiteral("heatdistortion"),
            QStringLiteral("lensdistort"),
            QStringLiteral("linearwipe"),
            QStringLiteral("parallax"),
            QStringLiteral("pinwheel"),
            QStringLiteral("pixelsort"),
            QStringLiteral("radialreveal"),
            QStringLiteral("smokedistortion"),
            QStringLiteral("splitscreenmasking"),
            QStringLiteral("surfaceraytrace"),
            QStringLiteral("verticalvideo"),
            QStringLiteral("wireremoval"),
            QStringLiteral("witnessprotection"),
            QStringLiteral("depthmatte"),
            QStringLiteral("goprolensreframe"),
            QStringLiteral("360bulge"),
            QStringLiteral("360magnify"),
            QStringLiteral("360twirl"),
            QStringLiteral("360unsharpen"),
            QStringLiteral("3dextrusion"),
            QStringLiteral("caustics"),
            QStringLiteral("clouds"),
            QStringLiteral("cosmos"),
            QStringLiteral("electro"),
            QStringLiteral("evaporate"),
            QStringLiteral("filmdamage"),
            QStringLiteral("gleam"),
            QStringLiteral("glint"),
            QStringLiteral("grainremoval"),
            QStringLiteral("halftonecolor"),
            QStringLiteral("hotspots"),
            QStringLiteral("hyperdrive"),
            QStringLiteral("innerglow"),
            QStringLiteral("lensdirt"),
            QStringLiteral("lightflares"),
            QStringLiteral("lightrays"),
            QStringLiteral("lightswordglow"),
            QStringLiteral("outerglow"),
            QStringLiteral("photorama"),
            QStringLiteral("proskinretouch"),
            QStringLiteral("radiowaves"),
            QStringLiteral("rainonglass"),
            QStringLiteral("setmatte"),
            QStringLiteral("shatter"),
            QStringLiteral("tvdamage"),
            QStringLiteral("watercaustics"),
            QStringLiteral("360animatedlasers"),
            QStringLiteral("360fractalnoise"),
            QStringLiteral("360lightsaberv2glow"),
            QStringLiteral("360lightsaberv2manual"),
            QStringLiteral("animatedlasers"),
            QStringLiteral("fractalnoise"),
            QStringLiteral("lightsaberv2glow"),
            QStringLiteral("lightsaberv2manual"),
            QStringLiteral("manuallightsword"),
            // Re-probed after completing the scratch texture/VBO/renderbuffer
            // services.  Each module below completes Notify(8/10), readback,
            // and an identical render on the dedicated render thread.
            QStringLiteral("anamorphiclensflare"),
            QStringLiteral("audiospectrum"),
            QStringLiteral("audiowaveform"),
            QStringLiteral("autovolumetrics"),
            QStringLiteral("blockdisplacement"),
            QStringLiteral("bloodsplat"),
            QStringLiteral("channelblur"),
            QStringLiteral("chromenator"),
            QStringLiteral("clonestamp"),
            QStringLiteral("coloradjustment"),
            QStringLiteral("colorama"),
            QStringLiteral("colormap"),
            QStringLiteral("denoise"),
            QStringLiteral("depthmask"),
            QStringLiteral("differencekey"),
            QStringLiteral("distancefield"),
            QStringLiteral("echo"),
            QStringLiteral("energydistortion"),
            QStringLiteral("environmentmaptransform"),
            QStringLiteral("environmentmapviewer"),
            QStringLiteral("erodewhite"),
            QStringLiteral("flame"),
            QStringLiteral("flicker"),
            QStringLiteral("forcemotionblur"),
            QStringLiteral("frameblendedretiming"),
            QStringLiteral("freezeframe"),
            QStringLiteral("glowimproved"),
            QStringLiteral("gradingtransfer"),
            QStringLiteral("jitterframes"),
            QStringLiteral("lightleak"),
            QStringLiteral("lightning"),
            QStringLiteral("lightwrap"),
            QStringLiteral("mattecleaner"),
            QStringLiteral("motionblur"),
            QStringLiteral("portal"),
            QStringLiteral("removefringe"),
            QStringLiteral("reverse"),
            QStringLiteral("roughedges"),
            QStringLiteral("shake"),
            QStringLiteral("timecode"),
            QStringLiteral("timedisplace"),
            QStringLiteral("timewarp"),
            // Recovered renderer capability flag, file-editor defaults,
            // complete multiline presets, mask picker/value and empty
            // mask/text-outline services.
            QStringLiteral("lightflaresv2"),
            QStringLiteral("lut"),
            QStringLiteral("stroke"),
            QStringLiteral("360lightsaberv2path"),
            QStringLiteral("lightsaberv2path"),
            QStringLiteral("vectorstroke"),
            QStringLiteral("360lightsaberv2auto"),
            QStringLiteral("atomicparticle"),
            QStringLiteral("automatedlightsword"),
            QStringLiteral("lightsaberv2auto"),
            QStringLiteral("pulpscifititlecrawl"),
            // RenderContext source UUID, real Notify(3/4) per-effect state and
            // RedrawCustomUI complete the text, tracking, scopes and Puppet
            // modules that keep asynchronous/cache state between frames.
            QStringLiteral("360text"),
            QStringLiteral("automaticstabilizer"),
            QStringLiteral("creditstextcrawl"),
            QStringLiteral("histogram"),
            QStringLiteral("motionlock"),
            QStringLiteral("puppet"),
            QStringLiteral("text"),
            QStringLiteral("vectorscope"),
            QStringLiteral("waveform"),
            QStringLiteral("waveformparade")
        };
        static const QSet<QString> verifiedNativeVideoTransitions {
            QStringLiteral("additivedissolve"),
            QStringLiteral("clockwipe"),
            QStringLiteral("crossdissolve"),
            QStringLiteral("crosszoom"),
            QStringLiteral("ditherdissolve"),
            QStringLiteral("fadetransition"),
            QStringLiteral("iris"),
            QStringLiteral("lightleaktransition"),
            QStringLiteral("linearwipetransition"),
            QStringLiteral("pageturntransition"),
            QStringLiteral("pushtransition"),
            QStringLiteral("radialwipe"),
            QStringLiteral("slidetransition"),
            QStringLiteral("split"),
            QStringLiteral("zoom")
        };
        static const QSet<QString> verifiedNativeAudioEffects {
            QStringLiteral("audioecho"),
            QStringLiteral("audioreverse"),
            QStringLiteral("balance"),
            QStringLiteral("cathedral"),
            QStringLiteral("channellevels"),
            QStringLiteral("compressor"),
            QStringLiteral("dopplershift"),
            QStringLiteral("equaliser"),
            QStringLiteral("largeroom"),
            QStringLiteral("mediumroom"),
            QStringLiteral("noisereduction"),
            QStringLiteral("pitch"),
            QStringLiteral("shortwaveradio"),
            QStringLiteral("smallroom"),
            QStringLiteral("telephone"),
            QStringLiteral("tone")
        };
        static const QSet<QString> verifiedNativeAudioTransitions {
            QStringLiteral("crossfade"),
            QStringLiteral("fade")
        };
        // TransformationAtTime/OpacityAtTime ABI (Notify 102/104). Each entry
        // completes with identical matrix/opacity output on the GUI and render
        // threads in tools/hfpl_runtime_probe's behavior mode. PositionMix and
        // RotateByLayer additionally exercise the layer transform services at
        // API+0x308..+0x330. Acceleration, Drag and Gravity use the shared
        // Notify(103) integrator. AttractTo, Follow and RepelFrom additionally
        // use the layer-state array and stable target FXID. Remaining modules
        // need more simulation or per-text-object callbacks.
        static const QSet<QString> verifiedNativeBehaviors {
            QStringLiteral("downinsert"),
            QStringLiteral("acceleration"),
            QStringLiteral("attractto"),
            QStringLiteral("drag"),
            QStringLiteral("downroll"),
            QStringLiteral("drop"),
            QStringLiteral("expansion"),
            QStringLiteral("fadebehavior"),
            QStringLiteral("flyinfadeout"),
            QStringLiteral("flyinflyout"),
            QStringLiteral("flytozoomin"),
            QStringLiteral("follow"),
            QStringLiteral("gravity"),
            QStringLiteral("leftroll"),
            QStringLiteral("positionmix"),
            QStringLiteral("repelfrom"),
            QStringLiteral("rightroll"),
            QStringLiteral("rotatebylayer"),
            QStringLiteral("stretchandzoomin"),
            QStringLiteral("tinyzoom"),
            QStringLiteral("twirlbehavior"),
            QStringLiteral("upinsert"),
            QStringLiteral("uproll"),
            QStringLiteral("zoomin"),
            QStringLiteral("zoomout")
        };
        const QString nativeBaseName = native.baseName.toLower();
        const bool verifiedFrameRenderer =
            spec.kind == PluginKind::Effect2D && native.lifecycleCompatible
            && verifiedNative2dRenderers.contains(nativeBaseName);
        const bool verifiedVideoTransition =
            spec.kind == PluginKind::VideoTransition && native.lifecycleCompatible
            && verifiedNativeVideoTransitions.contains(nativeBaseName);
        const bool verifiedAudioEffect =
            spec.kind == PluginKind::AudioEffect && native.lifecycleCompatible
            && verifiedNativeAudioEffects.contains(nativeBaseName);
        const bool verifiedAudioTransition =
            spec.kind == PluginKind::AudioTransition && native.lifecycleCompatible
            && verifiedNativeAudioTransitions.contains(nativeBaseName);
        const bool verifiedBehavior =
            spec.kind == PluginKind::BehaviorEffect && native.lifecycleCompatible
            && verifiedNativeBehaviors.contains(nativeBaseName);
        if (spec.kind == PluginKind::VideoTransition) {
            registerNativeVideoTransitionModule(spec.id, filePath, dependencyDir,
                                                verifiedVideoTransition,
                                                spec.parameters);
        } else if (spec.kind == PluginKind::AudioEffect) {
            registerNativeAudioEffectModule(spec.id, filePath, dependencyDir,
                                            verifiedAudioEffect, spec.parameters);
        } else if (spec.kind == PluginKind::AudioTransition) {
            registerNativeAudioTransitionModule(spec.id, filePath, dependencyDir,
                                                verifiedAudioTransition,
                                                spec.parameters);
        } else if (spec.kind == PluginKind::BehaviorEffect) {
            registerNativeBehaviorModule(spec.id, filePath, dependencyDir,
                                         verifiedBehavior, spec.parameters);
        } else {
            registerNativeEffectModule(spec.id, filePath, dependencyDir,
                                       verifiedFrameRenderer, spec.parameters);
        }
        spec.renderable = verifiedFrameRenderer || verifiedVideoTransition
                          || verifiedAudioEffect || verifiedBehavior;
        if (spec.renderable) {
            spec.unavailableReason.clear();
        } else if (verifiedAudioTransition) {
            spec.unavailableReason =
                QStringLiteral("The native audio-transition CPU renderer is available, but "
                               "timeline audio-overlap editing is not connected yet.");
        } else if (spec.kind == PluginKind::BehaviorEffect) {
            spec.unavailableReason =
                QStringLiteral("This Behavior uses the simulation or per-text-object ABI; "
                               "the TransformationAtTime/OpacityAtTime path is connected, "
                               "but this module's context is not yet available.");
        } else if (native.lifecycleChecked && !native.lifecycleCompatible) {
            spec.unavailableReason =
                QStringLiteral("This native module rejected the VEGAS Effects host during "
                               "initialization: %1.")
                    .arg(native.lifecycleError);
        } else {
            spec.unavailableReason =
                QStringLiteral("This native VEGAS Effects module loads and initializes. Its "
                               "Geometry/Behavior execution services are still being connected "
                               "to the renderer.");
        }
        spec.description =
            native.copyright.isEmpty()
                ? (native.vendor.isEmpty()
                       ? QStringLiteral("Native plugin module (%1)")
                             .arg(QFileInfo(filePath).fileName())
                       : QStringLiteral("Native plugin module (%1) by %2")
                             .arg(QFileInfo(filePath).fileName(), native.vendor))
                : QStringLiteral("Native plugin module (%1). %2")
                      .arg(QFileInfo(filePath).fileName(), native.copyright);

        PluginEntry entry;
        entry.plugin = std::make_shared<LocalPlugin>(filePath, spec);
        entry.spec = std::move(spec);
        m_entries.push_back(entry);
    }

    void registerVfxPreset(const QString& filePath)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            OV_LOG_WARN(QStringLiteral("Unable to read plugin preset: %1").arg(filePath));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isNull()) {
            OV_LOG_WARN(QStringLiteral("Invalid plugin preset JSON: %1").arg(filePath));
            return;
        }

        const QJsonObject root = doc.object();
        const QString id = root.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            return;
        }

        EffectSpec spec;
        spec.id = PluginId(id);
        spec.name = id;
        spec.displayName = root.value(QStringLiteral("displayName")).toString();
        if (spec.displayName.isEmpty()) {
            spec.displayName = id;
        }
        spec.category = root.value(QStringLiteral("category")).toString();
        if (spec.category.isEmpty()) {
            spec.category = QStringLiteral("Uncategorized");
        }
        spec.description = root.value(QStringLiteral("description")).toString();
        spec.version = core::Version::fromString(root.value(QStringLiteral("version")).toString());
        spec.kind = PluginKind::Effects;

        const QString type = root.value(QStringLiteral("type")).toString();
        if (type.compare(QStringLiteral("color"), Qt::CaseInsensitive) == 0) {
            spec.kind = PluginKind::ColorEffects;
        } else if (type.compare(QStringLiteral("generator"), Qt::CaseInsensitive) == 0) {
            spec.kind = PluginKind::Generators;
        } else if (type.compare(QStringLiteral("filter"), Qt::CaseInsensitive) == 0) {
            spec.kind = PluginKind::Filters;
        }

        // Parameters. Without this a .vfx could name an effect but never
        // describe a single control, so the Controls panel had nothing to build
        // and the widget vocabulary recovered from Tannen.dll's named
        // decompilation (PluginUICheckBox, PluginUILabel,
        // PluginUIMultiLineString, PluginUIDirectoryPicker, PluginUILayerPicker)
        // stayed unreachable.
        const QJsonArray params = root.value(QStringLiteral("parameters")).toArray();
        for (const QJsonValue& value : params) {
            const QJsonObject p = value.toObject();
            EffectParameterSpec param;
            param.name = p.value(QStringLiteral("name")).toString();
            if (param.name.isEmpty()) {
                continue;
            }
            param.displayName = p.value(QStringLiteral("displayName")).toString();
            if (param.displayName.isEmpty()) {
                param.displayName = param.name;
            }
            param.type = p.value(QStringLiteral("type")).toString();
            if (param.type.isEmpty()) {
                param.type = QStringLiteral("string");
            }
            // "default" is the natural spelling in JSON; "defaultValue" is
            // accepted too so a file written against the struct still loads.
            QJsonValue def = p.value(QStringLiteral("default"));
            if (def.isUndefined()) {
                def = p.value(QStringLiteral("defaultValue"));
            }
            param.defaultValue = def.isBool()
                                     ? (def.toBool() ? QStringLiteral("true")
                                                     : QStringLiteral("false"))
                                     : def.toVariant().toString();
            param.unit = p.value(QStringLiteral("unit")).toString();
            param.minimum = p.value(QStringLiteral("minimum")).toDouble(param.minimum);
            param.maximum = p.value(QStringLiteral("maximum")).toDouble(param.maximum);
            param.decimals = p.value(QStringLiteral("decimals")).toInt(param.decimals);
            param.step = p.value(QStringLiteral("step")).toDouble(param.step);
            param.group = p.value(QStringLiteral("group")).toString();
            for (const auto& choice : p.value(QStringLiteral("choices")).toArray()) param.choices.append(choice.toString());
            spec.parameters.push_back(param);
        }

        PluginEntry entry;
        entry.plugin = std::make_shared<LocalPlugin>(filePath, spec);
        entry.spec = std::move(spec);
        m_entries.push_back(entry);
    }

    static constexpr int kMetadataCacheVersion = 11;

    QVector<QString> m_dirs;
    Callbacks m_callbacks;
    QHash<QString, QJsonObject> m_metadataCache;
    bool m_cacheDirty = false;
    int indexOfId(const QString& id) const
    {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).spec.id.value() == id) {
                return i;
            }
        }
        return -1;
    }

    QVector<PluginEntry> m_entries;
};

std::shared_ptr<PluginManager> createPluginManager()
{
    return std::make_shared<DefaultPluginManager>();
}

} // namespace plugin
} // namespace openvegas
