#include "app/AVTemplates.h"

#include "composition/Composition.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>

namespace openvegas {
namespace app {
namespace {

QString s_folderOverride;

QString childText(const QDomElement& parent, const char* name)
{
    return parent.firstChildElement(QString::fromLatin1(name)).text();
}

AVTemplate builtIn(const char* id, const char* name, int width, int height, double fps)
{
    AVTemplate t;
    t.id = QString::fromLatin1(id);
    t.name = QCoreApplication::translate("AVTemplate", name);
    t.system = true;
    t.width = width;
    t.height = height;
    t.frameRate = fps;
    return t;
}

} // namespace

QVector<AVTemplate> builtInTemplates()
{
    // The reference's system templates by their own names (its translation
    // catalogues list them under "AVTemplate"); the sizes are the formats'
    // standard ones, since the installed .hft files are not available here.
    struct Row { const char* id; const char* name; int width; int height; double fps; };
    static const Row rows[] = {
        {"fullhd23976", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 23.976 fps"), 1920, 1080, 24000.0 / 1001.0},
        {"fullhd24", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 24 fps"), 1920, 1080, 24.0},
        {"fullhd25", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 25 fps"), 1920, 1080, 25.0},
        {"fullhd2997", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 29.97 fps"), 1920, 1080, 30000.0 / 1001.0},
        {"fullhd30", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 30 fps"), 1920, 1080, 30.0},
        {"fullhd50", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 50 fps"), 1920, 1080, 50.0},
        {"fullhd5994", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 59.94 fps"), 1920, 1080, 60000.0 / 1001.0},
        {"fullhd60", QT_TRANSLATE_NOOP("AVTemplate", "1080p Full HD @ 60 fps"), 1920, 1080, 60.0},
        {"hd23976", QT_TRANSLATE_NOOP("AVTemplate", "720p HD @ 23.976 fps"), 1280, 720, 24000.0 / 1001.0},
        {"hd25", QT_TRANSLATE_NOOP("AVTemplate", "720p HD @ 25 fps"), 1280, 720, 25.0},
        {"hd2997", QT_TRANSLATE_NOOP("AVTemplate", "720p HD @ 29.97 fps"), 1280, 720, 30000.0 / 1001.0},
        {"hd30", QT_TRANSLATE_NOOP("AVTemplate", "720p HD @ 30 fps"), 1280, 720, 30.0},
        {"uhd23976", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 23.976 fps"), 3840, 2160, 24000.0 / 1001.0},
        {"uhd24", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 24 fps"), 3840, 2160, 24.0},
        {"uhd25", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 25 fps"), 3840, 2160, 25.0},
        {"uhd2997", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 29.97 fps"), 3840, 2160, 30000.0 / 1001.0},
        {"uhd30", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 30 fps"), 3840, 2160, 30.0},
        {"uhd50", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 50 fps"), 3840, 2160, 50.0},
        {"uhd5994", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 59.94 fps"), 3840, 2160, 60000.0 / 1001.0},
        {"uhd60", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 60 fps"), 3840, 2160, 60.0},
        {"uhd100", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 100 fps"), 3840, 2160, 100.0},
        {"uhd11988", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 119.88 fps"), 3840, 2160, 120000.0 / 1001.0},
        {"uhd120", QT_TRANSLATE_NOOP("AVTemplate", "4K UHD @ 120 fps"), 3840, 2160, 120.0},
        {"uhd8k23976", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 23.976 fps"), 7680, 4320, 24000.0 / 1001.0},
        {"uhd8k24", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 24 fps"), 7680, 4320, 24.0},
        {"uhd8k25", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 25 fps"), 7680, 4320, 25.0},
        {"uhd8k2997", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 29.97 fps"), 7680, 4320, 30000.0 / 1001.0},
        {"uhd8k30", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 30 fps"), 7680, 4320, 30.0},
        {"uhd8k50", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 50 fps"), 7680, 4320, 50.0},
        {"uhd8k5994", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 59.94 fps"), 7680, 4320, 60000.0 / 1001.0},
        {"uhd8k60", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 60 fps"), 7680, 4320, 60.0},
        {"uhd8k100", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 100 fps"), 7680, 4320, 100.0},
        {"uhd8k11988", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 119.88 fps"), 7680, 4320, 120000.0 / 1001.0},
        {"uhd8k120", QT_TRANSLATE_NOOP("AVTemplate", "8K UHD @ 120 fps"), 7680, 4320, 120.0},
        {"dci4k24", QT_TRANSLATE_NOOP("AVTemplate", "4K DCI 2160p @ 24 fps"), 4096, 2160, 24.0},
        {"dci4k25", QT_TRANSLATE_NOOP("AVTemplate", "4K DCI 2160p @ 25 fps"), 4096, 2160, 25.0},
        {"dci4k2997", QT_TRANSLATE_NOOP("AVTemplate", "4K DCI 2160p @ 29.97 fps"), 4096, 2160, 30000.0 / 1001.0},
        {"dci4k30", QT_TRANSLATE_NOOP("AVTemplate", "4K DCI 2160p @ 30 fps"), 4096, 2160, 30.0},
        {"dci2k24", QT_TRANSLATE_NOOP("AVTemplate", "2K DCI @ 24 fps"), 2048, 1080, 24.0},
        {"dci2k25", QT_TRANSLATE_NOOP("AVTemplate", "2K DCI @ 25 fps"), 2048, 1080, 25.0},
        {"dci2k2997", QT_TRANSLATE_NOOP("AVTemplate", "2K DCI @ 29.97 fps"), 2048, 1080, 30000.0 / 1001.0},
        {"dci2k30", QT_TRANSLATE_NOOP("AVTemplate", "2K DCI @ 30 fps"), 2048, 1080, 30.0},
        {"qwxga24", QT_TRANSLATE_NOOP("AVTemplate", "2K 16:9 QWXGA @ 24 fps"), 2048, 1152, 24.0},
        {"qwxga25", QT_TRANSLATE_NOOP("AVTemplate", "2K 16:9 QWXGA @ 25 fps"), 2048, 1152, 25.0},
        {"qwxga2997", QT_TRANSLATE_NOOP("AVTemplate", "2K 16:9 QWXGA @ 29.97 fps"), 2048, 1152, 30000.0 / 1001.0},
        {"qwxga30", QT_TRANSLATE_NOOP("AVTemplate", "2K 16:9 QWXGA @ 30 fps"), 2048, 1152, 30.0},
        {"qhd24", QT_TRANSLATE_NOOP("AVTemplate", "1440p QHD @ 24 fps"), 2560, 1440, 24.0},
        {"qhd25", QT_TRANSLATE_NOOP("AVTemplate", "1440p QHD @ 25 fps"), 2560, 1440, 25.0},
        {"qhd30", QT_TRANSLATE_NOOP("AVTemplate", "1440p QHD @ 30 fps"), 2560, 1440, 30.0},
        {"qhd48", QT_TRANSLATE_NOOP("AVTemplate", "1440p QHD @ 48 fps"), 2560, 1440, 48.0},
        {"qhd60", QT_TRANSLATE_NOOP("AVTemplate", "1440p QHD @ 60 fps"), 2560, 1440, 60.0},
        {"gopro144024", QT_TRANSLATE_NOOP("AVTemplate", "1440p GoPro @ 24 fps"), 1920, 1440, 24.0},
        {"gopro144025", QT_TRANSLATE_NOOP("AVTemplate", "1440p GoPro @ 25 fps"), 1920, 1440, 25.0},
        {"gopro144030", QT_TRANSLATE_NOOP("AVTemplate", "1440p GoPro @ 30 fps"), 1920, 1440, 30.0},
        {"gopro144048", QT_TRANSLATE_NOOP("AVTemplate", "1440p GoPro @ 48 fps"), 1920, 1440, 48.0},
        {"gopro144060", QT_TRANSLATE_NOOP("AVTemplate", "1440p GoPro @ 60 fps"), 1920, 1440, 60.0},
        {"gopro27k24", QT_TRANSLATE_NOOP("AVTemplate", "2.7K GoPro @ 24 fps"), 2704, 1520, 24.0},
        {"gopro27k25", QT_TRANSLATE_NOOP("AVTemplate", "2.7K GoPro @ 25 fps"), 2704, 1520, 25.0},
        {"gopro27k30", QT_TRANSLATE_NOOP("AVTemplate", "2.7K GoPro @ 30 fps"), 2704, 1520, 30.0},
        {"gopro27k48", QT_TRANSLATE_NOOP("AVTemplate", "2.7K GoPro @ 48 fps"), 2704, 1520, 48.0},
        {"gopro27k60", QT_TRANSLATE_NOOP("AVTemplate", "2.7K GoPro @ 60 fps"), 2704, 1520, 60.0},
        {"bmcc23976", QT_TRANSLATE_NOOP("AVTemplate", "2.5K BMCC @ 23.976 fps"), 2432, 1366, 24000.0 / 1001.0},
        {"bmcc24", QT_TRANSLATE_NOOP("AVTemplate", "2.5K BMCC @ 24 fps"), 2432, 1366, 24.0},
        {"bmcc25", QT_TRANSLATE_NOOP("AVTemplate", "2.5K BMCC @ 25 fps"), 2432, 1366, 25.0},
        {"bmcc2997", QT_TRANSLATE_NOOP("AVTemplate", "2.5K BMCC @ 29.97 fps"), 2432, 1366, 30000.0 / 1001.0},
        {"bmcc30", QT_TRANSLATE_NOOP("AVTemplate", "2.5K BMCC @ 30 fps"), 2432, 1366, 30.0},
        {"redepic24", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 24 fps"), 5120, 2700, 24.0},
        {"redepic25", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 25 fps"), 5120, 2700, 25.0},
        {"redepic30", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 30 fps"), 5120, 2700, 30.0},
        {"redepic48", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 48 fps"), 5120, 2700, 48.0},
        {"redepic60", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 60 fps"), 5120, 2700, 60.0},
        {"redepic100", QT_TRANSLATE_NOOP("AVTemplate", "5K RED EPIC @ 100 fps"), 5120, 2700, 100.0},
        {"reddragon24", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 24 fps"), 6144, 3160, 24.0},
        {"reddragon25", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 25 fps"), 6144, 3160, 25.0},
        {"reddragon30", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 30 fps"), 6144, 3160, 30.0},
        {"reddragon48", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 48 fps"), 6144, 3160, 48.0},
        {"reddragon60", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 60 fps"), 6144, 3160, 60.0},
        {"reddragon100", QT_TRANSLATE_NOOP("AVTemplate", "6K RED DRAGON @ 100 fps"), 6144, 3160, 100.0},
        {"instagram30", QT_TRANSLATE_NOOP("AVTemplate", "Instagram Square @ 30 fps"), 1080, 1080, 30.0},
        {"vertical25", QT_TRANSLATE_NOOP("AVTemplate", "Vertical 1080p @ 25 fps"), 1080, 1920, 25.0},
        {"vertical30", QT_TRANSLATE_NOOP("AVTemplate", "Vertical 1080p @ 30 fps"), 1080, 1920, 30.0},
    };
    QVector<AVTemplate> templates;
    for (const Row& row : rows) templates.append(builtIn(row.id, row.name, row.width, row.height, row.fps));
    return templates;
}

QString userTemplateFolder()
{
    if (!s_folderOverride.isEmpty()) return s_folderOverride;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("Templates/AV"));
}

void setUserTemplateFolderOverride(const QString& folder) { s_folderOverride = folder; }

QVector<AVTemplate> loadTemplates(const QString& folder)
{
    QVector<AVTemplate> templates;
    const QDir dir(folder);
    for (const QString& name : dir.entryList({QStringLiteral("*.hft")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        QDomDocument doc;
        if (!file.open(QIODevice::ReadOnly) || !doc.setContent(file.readAll())) continue;
        const QDomElement root = doc.documentElement();
        if (root.tagName() != QLatin1String("Templates")) continue;
        for (QDomElement node = root.firstChildElement(QStringLiteral("Template")); !node.isNull();
             node = node.nextSiblingElement(QStringLiteral("Template"))) {
            AVTemplate t;
            t.filePath = file.fileName();
            t.system = childText(node, "SystemTemplate").toUShort() != 0;
            t.name = childText(node, "Name");
            t.id = childText(node, "ID");
            if (t.id.isEmpty()) t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            t.width = childText(node, "Width").toInt();
            t.height = childText(node, "Height").toInt();
            t.frameRate = childText(node, "FrameRate").toDouble();
            t.pixelAspect = childText(node, "PAR").toInt();
            t.pixelAspectValue = childText(node, "PARValue").toDouble();
            // A named aspect has its own value; only Custom keeps the stored one.
            if (t.pixelAspect != composition::Composition::CustomAspect)
                t.pixelAspectValue = composition::Composition::pixelAspectValue(t.pixelAspect, 1.0);
            t.audioSampleRate = childText(node, "AudioSampleRate").toInt();
            if (t.audioSampleRate <= 0) t.audioSampleRate = 48000;
            if (t.width > 0 && t.height > 0 && t.frameRate > 0.0) templates.append(t);
        }
    }
    return templates;
}

QVector<AVTemplate> allTemplates()
{
    QVector<AVTemplate> templates = builtInTemplates();
    templates += loadTemplates(userTemplateFolder());
    return templates;
}

AVTemplate templateById(const QString& id)
{
    for (const AVTemplate& t : allTemplates())
        if (t.id == id) return t;
    return AVTemplate{QString(), QString()};
}

AVTemplate saveUserTemplate(AVTemplate t)
{
    if (t.id.isEmpty() || t.system) t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.system = false;
    const QString folder = userTemplateFolder();
    if (!QDir().mkpath(folder)) return AVTemplate{QString(), QString()};
    QDomDocument doc;
    doc.appendChild(doc.createProcessingInstruction(QStringLiteral("xml"),
                                                    QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = doc.createElement(QStringLiteral("Templates"));
    doc.appendChild(root);
    QDomElement node = doc.createElement(QStringLiteral("Template"));
    node.setAttribute(QStringLiteral("Version"), QStringLiteral("0"));
    root.appendChild(node);
    const auto add = [&](const char* name, const QString& value) {
        QDomElement child = doc.createElement(QString::fromLatin1(name));
        child.appendChild(doc.createTextNode(value));
        node.appendChild(child);
    };
    add("SystemTemplate", QStringLiteral("0"));
    add("ID", t.id);
    add("Name", t.name);
    add("Width", QString::number(t.width));
    add("Height", QString::number(t.height));
    add("FrameRate", QString::number(std::round(t.frameRate * 1000.0) / 1000.0, 'g', 10));
    add("PAR", QString::number(t.pixelAspect));
    add("PARValue", QString::number(t.pixelAspectValue, 'g', 8));
    add("AudioSampleRate", QString::number(t.audioSampleRate));
    t.filePath = QDir(folder).filePath(t.id + QStringLiteral(".hft"));
    QSaveFile file(t.filePath);
    if (!file.open(QIODevice::WriteOnly)) return AVTemplate{QString(), QString()};
    file.write(doc.toByteArray(1));
    if (!file.commit()) return AVTemplate{QString(), QString()};
    return t;
}

bool deleteUserTemplate(const AVTemplate& t)
{
    if (t.system || t.filePath.isEmpty()) return false;
    return QFile::remove(t.filePath);
}

} // namespace app
} // namespace openvegas
