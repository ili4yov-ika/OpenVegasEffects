#include "ui/ExportPresets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "app/Settings.h"
#include "core/Log.h"

// Export presets in the interface: ExportPresetManager keeps the built-in
// presets and the user's (one file each in the user preset folder) and the
// default one; ExportPresetDialog edits a preset's properties as a tree that
// shows only what applies to its format and codec; ExportPresetsPage is the
// Export panel's Presets tab - the list, search, the default preset and the
// create, edit, rename, duplicate and delete commands. Built-in presets are
// never changed: editing one saves a copy.

namespace openvegas {
namespace ui {

namespace {

using render::ExportFormat;
using render::ExportPreset;

const QString kDefaultKey = QStringLiteral("Options/DefaultExportPreset");

// The settings file the default preset is kept in (the application's own,
// not the default QSettings scope).
QSettings exportSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, app::Settings::organizationName(),
                     app::Settings::applicationName());
}

// A text in a context chosen at run time (the property names are marked
// for lupdate in kPropertyNames and the choice lists).
QString translated(const char* context, const char* text)
{
    return QCoreApplication::translate(context, text);
}

// The translation context of a format's property names.
const char* formatContext(ExportFormat format)
{
    switch (format) {
    case ExportFormat::MP4: return "MP4ExportPreset";
    case ExportFormat::AVI: return "AVIExportPreset";
    case ExportFormat::MOV: return "MOVExportPreset";
    case ExportFormat::WAV: return "WAVExportPreset";
    case ExportFormat::ImageSequence: return "ImageSequenceExportPreset";
    }
    return "MP4ExportPreset";
}

// The names the properties and their choices are shown with, marked in the
// contexts the reference translates them in.
const char* const kScaleModes[] = {QT_TRANSLATE_NOOP("MP4ExportPreset", "Ignore Aspect Ratio"),
                                   QT_TRANSLATE_NOOP("MP4ExportPreset", "Keep Aspect Ratio"),
                                   QT_TRANSLATE_NOOP("MP4ExportPreset", "Keep Aspect Ratio By Expanding")};
const char* const kProfiles[] = {QT_TRANSLATE_NOOP("MP4ExportPreset", "Baseline"),
                                 QT_TRANSLATE_NOOP("MP4ExportPreset", "Main"),
                                 QT_TRANSLATE_NOOP("MP4ExportPreset", "High")};
const char* const kEncodings[] = {QT_TRANSLATE_NOOP("MP4ExportPreset", "Constant Bitrate"),
                                  QT_TRANSLATE_NOOP("MP4ExportPreset", "Variable Bitrate, 1 pass")};
const char* const kMovCodecs[] = {QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 422 (Proxy)"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 422 (LT)"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 422"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 422 (HQ)"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 4444"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Apple ProRes 4444 XQ"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "AVC / H.264"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "Photo-JPEG"),
                                  QT_TRANSLATE_NOOP("MOVExportPreset", "GoPro CineForm")};
const char* const kAviCodecs[] = {QT_TRANSLATE_NOOP("AVIExportPreset", "Uncompressed"),
                                  QT_TRANSLATE_NOOP("AVIExportPreset", "GoPro CineForm"),
                                  QT_TRANSLATE_NOOP("AVIExportPreset", "DV NTSC"),
                                  QT_TRANSLATE_NOOP("AVIExportPreset", "DV PAL")};
const char* const kCineFormFormats[] = {QT_TRANSLATE_NOOP("MOVExportPreset", "10-bit YUV 4:2:2"),
                                        QT_TRANSLATE_NOOP("MOVExportPreset", "12-bit RGB/RGBA")};
const char* const kImageFormats[] = {QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "PNG"),
                                     QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "JPG"),
                                     QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "BMP"),
                                     QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "OpenEXR")};
const char* const kChannels[] = {QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "RGB"),
                                 QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "RGBA")};
const char* const kMovAudioCodecs[] = {QT_TRANSLATE_NOOP("MOVExportPreset", "Uncompressed PCM"),
                                       QT_TRANSLATE_NOOP("MOVExportPreset", "AAC")};
const int kSampleRates[] = {8000, 12000, 16000, 24000, 32000, 44100, 48000, 88200, 96000};
const int kAacBitRates[] = {64, 96, 112, 128, 160, 192, 224, 256, 320};

// Names of the property rows, so lupdate sees each in every context the
// reference translates it in.
[[maybe_unused]] const char* const kPropertyNames[] = {
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Video"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Audio"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Width"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Height"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Scale Mode"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Frame Rate"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Codec"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Profile"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Encoding"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Bitrate"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Target Bitrate"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Max. Bitrate"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Sample Rate"), QT_TRANSLATE_NOOP("MP4ExportPreset", "Channels"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "H.264"), QT_TRANSLATE_NOOP("MP4ExportPreset", "AAC"),
    QT_TRANSLATE_NOOP("MP4ExportPreset", "Stereo"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Video"), QT_TRANSLATE_NOOP("MOVExportPreset", "Audio"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Width"), QT_TRANSLATE_NOOP("MOVExportPreset", "Height"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Scale Mode"), QT_TRANSLATE_NOOP("MOVExportPreset", "Frame Rate"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Codec"), QT_TRANSLATE_NOOP("MOVExportPreset", "Bitrate"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Key Frame Interval"), QT_TRANSLATE_NOOP("MOVExportPreset", "Quality"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Format"), QT_TRANSLATE_NOOP("MOVExportPreset", "Channels"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Sample Rate"), QT_TRANSLATE_NOOP("MOVExportPreset", "Stereo"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Ignore Aspect Ratio"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Keep Aspect Ratio"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "Keep Aspect Ratio By Expanding"),
    QT_TRANSLATE_NOOP("MOVExportPreset", "RGB"), QT_TRANSLATE_NOOP("MOVExportPreset", "RGBA"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Video"), QT_TRANSLATE_NOOP("AVIExportPreset", "Audio"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Width"), QT_TRANSLATE_NOOP("AVIExportPreset", "Height"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Scale Mode"), QT_TRANSLATE_NOOP("AVIExportPreset", "Frame Rate"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Codec"), QT_TRANSLATE_NOOP("AVIExportPreset", "Format"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Channels"), QT_TRANSLATE_NOOP("AVIExportPreset", "Sample Rate"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "PCM"), QT_TRANSLATE_NOOP("AVIExportPreset", "Stereo"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Ignore Aspect Ratio"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Keep Aspect Ratio"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "Keep Aspect Ratio By Expanding"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "10-bit YUV 4:2:2"), QT_TRANSLATE_NOOP("AVIExportPreset", "12-bit RGB/RGBA"),
    QT_TRANSLATE_NOOP("AVIExportPreset", "RGB"), QT_TRANSLATE_NOOP("AVIExportPreset", "RGBA"),
    QT_TRANSLATE_NOOP("WAVExportPreset", "Audio"), QT_TRANSLATE_NOOP("WAVExportPreset", "Codec"),
    QT_TRANSLATE_NOOP("WAVExportPreset", "Sample Rate"), QT_TRANSLATE_NOOP("WAVExportPreset", "Channels"),
    QT_TRANSLATE_NOOP("WAVExportPreset", "PCM"), QT_TRANSLATE_NOOP("WAVExportPreset", "Stereo"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Video"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Width"), QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Height"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Scale Mode"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Format"), QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Quality"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Channels"), QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Prefix"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Ignore Aspect Ratio"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Keep Aspect Ratio"),
    QT_TRANSLATE_NOOP("ImageSequenceExportPreset", "Keep Aspect Ratio By Expanding"),
};

} // namespace

// --- ExportPresetManager ---------------------------------------------------

ExportPresetManager::ExportPresetManager(const QString& userDirectory, QObject* parent)
    : QObject(parent), m_directory(userDirectory)
{
    // As the reference's constructor does: a default when none was chosen.
    QSettings settings = exportSettings();
    if (settings.value(kDefaultKey).toString().isEmpty())
        settings.setValue(kDefaultKey, render::defaultExportPresetName());
    reload();
}

// The file of a user preset: <folder>/<name>.<suffix>.
QString ExportPresetManager::pathFor(const QString& name) const
{
    return QDir(m_directory).filePath(name + QLatin1Char('.') + render::exportPresetFileSuffix());
}

// Re-reads the presets: the built-in ones, then the user's sorted by name.
// Unreadable files are logged and skipped.
void ExportPresetManager::reload()
{
    m_presets = render::builtInExportPresets();
    QVector<ExportPreset> user;
    const QDir dir(m_directory);
    bool failed = false;
    for (const QFileInfo& file : dir.entryInfoList({QStringLiteral("*.") + render::exportPresetFileSuffix()},
                                                   QDir::Files, QDir::Name)) {
        ExportPreset preset;
        QString error;
        if (!render::readExportPreset(file.absoluteFilePath(), &preset, &error)) {
            OV_LOG_WARN(QStringLiteral("Export preset %1: %2").arg(file.fileName(), error));
            failed = true;
            continue;
        }
        preset.userCreated = true;   // whatever the file says, it is the user's to change
        user.append(preset);
    }
    if (failed) OV_LOG_WARN(QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "Some export presets could not be loaded."));
    std::sort(user.begin(), user.end(), [](const ExportPreset& a, const ExportPreset& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    m_presets += user;
    emit presetsChanged();
}

// A preset by name, or an invalid one.
ExportPreset ExportPresetManager::preset(const QString& name) const
{
    for (const ExportPreset& preset : m_presets)
        if (preset.name == name) return preset;
    return ExportPreset{};
}

// The preset new export tasks use; the built-in default when the chosen one
// no longer exists.
QString ExportPresetManager::defaultPreset() const
{
    const QString name = exportSettings().value(kDefaultKey).toString();
    return contains(name) ? name : render::defaultExportPresetName();
}

bool ExportPresetManager::setDefaultPreset(const QString& name, QString* error)
{
    if (!contains(name)) {
        if (error) *error = QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "The preset '%1' could not be set as the default.").arg(name);
        return false;
    }
    QSettings settings = exportSettings();
    settings.setValue(kDefaultKey, name);
    emit presetsChanged();
    return true;
}

// Saves a user preset. previousName is the name it was edited under (empty
// for a new one): renaming writes the new file, removes the old one and
// carries the default over. A new name that is already taken is refused.
bool ExportPresetManager::savePreset(ExportPreset preset, const QString& previousName, QString* error)
{
    preset.userCreated = true;
    preset.name = preset.name.trimmed();
    if (preset.name.isEmpty() || !QDir().mkpath(m_directory)) {
        if (error) {
            *error = preset.name.isEmpty()
                ? QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "The preset could not be added.")
                : QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "The export preset directory '%1' does not exist or could not be created.")
                      .arg(QDir::toNativeSeparators(m_directory));
        }
        return false;
    }
    const bool renamed = !previousName.isEmpty() && previousName != preset.name;
    const ExportPreset existing = this->preset(preset.name);
    if ((renamed || previousName.isEmpty()) && existing.isValid()) {
        if (error) {
            *error = QCoreApplication::translate("biff::ui::exporter::ExportPresetManager",
                        "The export preset '%1' could not be renamed because a preset or other file with the name '%2' already exists.")
                         .arg(previousName.isEmpty() ? preset.name : previousName, preset.name);
        }
        return false;
    }
    QString writeError;
    if (!render::writeExportPreset(pathFor(preset.name), preset, &writeError)) {
        if (error) *error = QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "The export preset '%1' could not be saved.").arg(preset.name);
        OV_LOG_WARN(writeError);
        return false;
    }
    if (renamed) {
        QFile::remove(pathFor(previousName));
        QSettings settings = exportSettings();
        if (settings.value(kDefaultKey).toString() == previousName) settings.setValue(kDefaultKey, preset.name);
    }
    reload();
    return true;
}

// Deletes user presets (built-in ones are skipped); if the default was one
// of them, the built-in default takes its place.
bool ExportPresetManager::removePresets(const QStringList& names, QString* error)
{
    bool ok = true;
    for (const QString& name : names) {
        const ExportPreset preset = this->preset(name);
        if (!preset.isValid() || !preset.userCreated) continue;   // built-in ones stay
        ok = QFile::remove(pathFor(name)) && ok;
    }
    if (!ok && error) *error = QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "One or more export presets could not be removed.");
    QSettings settings = exportSettings();
    if (names.contains(settings.value(kDefaultKey).toString()))
        settings.setValue(kDefaultKey, render::defaultExportPresetName());
    reload();
    return ok;
}

// Saves a copy of each preset under a free name ("<name> 1", ...); returns
// the names made.
QStringList ExportPresetManager::duplicatePresets(const QStringList& names, QString* error)
{
    QStringList made;
    bool ok = true;
    for (const QString& name : names) {
        ExportPreset copy = preset(name);
        if (!copy.isValid()) continue;
        copy.name = uniqueName(name);
        copy.userCreated = true;
        if (render::writeExportPreset(pathFor(copy.name), copy) || (QDir().mkpath(m_directory)
                                                                    && render::writeExportPreset(pathFor(copy.name), copy))) {
            made << copy.name;
            m_presets.append(copy);   // so the next unique name sees it
        } else {
            ok = false;
        }
    }
    if (!ok && error) *error = QCoreApplication::translate("biff::ui::exporter::ExportPresetManager", "One or more export presets could not be duplicated.");
    reload();
    return made;
}

// The name itself if it is free (no preset and no file of that name),
// otherwise the name with the first free number appended.
QString ExportPresetManager::uniqueName(const QString& base) const
{
    const QString trimmed = base.trimmed().isEmpty() ? QCoreApplication::translate("ExportPreset", "New Preset") : base.trimmed();
    if (!contains(trimmed) && !QFileInfo::exists(pathFor(trimmed))) return trimmed;
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(trimmed).arg(n);
        if (!contains(candidate) && !QFileInfo::exists(pathFor(candidate))) return candidate;
    }
}

// --- ExportPresetDialog ----------------------------------------------------

ExportPresetDialog::ExportPresetDialog(const ExportPreset& preset, bool creating,
                                       std::function<bool(const QString&)> nameTaken, QWidget* parent)
    : QDialog(parent), m_preset(preset), m_nameTaken(std::move(nameTaken))
{
    setObjectName(QStringLiteral("export-preset-dialog"));
    setWindowTitle(QCoreApplication::translate("ExportPresetDialog", "Edit Preset"));
    resize(520, 560);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(m_preset.name, this);
    m_name->setObjectName(QStringLiteral("lineEditPresetName"));
    m_name->setToolTip(QCoreApplication::translate("ExportPresetDialog", "The name for this preset"));
    form->addRow(QCoreApplication::translate("ExportPresetDialog", "Name"), m_name);
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("comboBoxPresetFormat"));
    for (ExportFormat format : {ExportFormat::MP4, ExportFormat::MOV, ExportFormat::AVI, ExportFormat::WAV,
                                ExportFormat::ImageSequence}) {
        m_format->addItem(render::exportFormatName(format), int(format));
    }
    m_format->setCurrentIndex(m_format->findData(int(m_preset.format)));
    // The kind of a saved preset is its file's; only a new one chooses.
    m_format->setEnabled(creating);
    form->addRow(QCoreApplication::translate("ExportPresetDialog", "Format:"), m_format);
    m_comment = new QLineEdit(m_preset.comment, this);
    m_comment->setObjectName(QStringLiteral("lineEditPresetComment"));
    m_comment->setPlaceholderText(QCoreApplication::translate("ExportPresetDialog", "Click here to enter a comment for this preset"));
    m_comment->setToolTip(QCoreApplication::translate("ExportPresetDialog", "The user comment for this preset"));
    form->addRow(QCoreApplication::translate("ExportPresetDialog", "Comment:"), m_comment);
    layout->addLayout(form);

    auto* propertiesLabel = new QLabel(QCoreApplication::translate("ExportPresetDialog", "Properties"), this);
    layout->addWidget(propertiesLabel);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("lineEditPropertySearch"));
    m_search->setPlaceholderText(QCoreApplication::translate("ExportPresetDialog", "Search in Properties"));
    m_search->setToolTip(QCoreApplication::translate("ExportPresetDialog", "Search for a property by name"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    m_properties = new QTreeWidget(this);
    m_properties->setObjectName(QStringLiteral("treePresetProperties"));
    m_properties->setToolTip(QCoreApplication::translate("ExportPresetDialog", "The properties for this preset"));
    m_properties->setColumnCount(2);
    m_properties->setHeaderLabels({QCoreApplication::translate("ExportPresetPropertiesItemModel", "Property"),
                                   QCoreApplication::translate("ExportPresetPropertiesItemModel", "Value")});
    m_properties->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_properties->header()->setStretchLastSection(true);
    layout->addWidget(m_properties, 1);
    m_problems = new QLabel(this);
    m_problems->setObjectName(QStringLiteral("labelPresetError"));
    m_problems->setWordWrap(true);
    m_problems->setStyleSheet(QStringLiteral("color: #e0a040;"));
    layout->addWidget(m_problems);

    auto* buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(m_preset.userCreated || creating ? QCoreApplication::translate("ExportPresetDialog", "OK")
                                                               : QCoreApplication::translate("ExportPresetTreeView", "Save a Copy"),
                              QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("pushButtonPresetOk"));
    buttons->addButton(QCoreApplication::translate("ExportPresetDialog", "Cancel"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_preset.name = text.trimmed();
        updateProblems();
    });
    connect(m_comment, &QLineEdit::textChanged, this, [this](const QString& text) { m_preset.comment = text; });
    // A new format starts with the streams and audio codec it can have.
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        const ExportFormat format = ExportFormat(m_format->currentData().toInt());
        if (format == m_preset.format) return;
        m_preset.format = format;
        m_preset.exportVideo = format != ExportFormat::WAV;
        m_preset.exportAudio = format != ExportFormat::ImageSequence;
        m_preset.audioCodec = format == ExportFormat::MP4 ? ExportPreset::AudioCodec::Aac
                                                          : ExportPreset::AudioCodec::Pcm;
        rebuildProperties();
    });
    // The search shows matching properties, and whole groups by their name.
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int g = 0; g < m_properties->topLevelItemCount(); ++g) {
            QTreeWidgetItem* group = m_properties->topLevelItem(g);
            bool any = false;
            for (int r = 0; r < group->childCount(); ++r) {
                QTreeWidgetItem* row = group->child(r);
                const bool shown = text.isEmpty() || row->text(0).contains(text, Qt::CaseInsensitive);
                row->setHidden(!shown);
                any = any || shown;
            }
            group->setHidden(!any && !group->text(0).contains(text, Qt::CaseInsensitive));
        }
    });
    rebuildProperties();
}

// Builds the property tree for the preset as it is now: a Video group (with
// its switch where video is optional) - size, scale mode, frame rate, codec
// or image format, and the H.264, JPEG quality, CineForm and alpha settings
// when they apply - and an Audio group. Choices that change which rows exist
// (codec, encoding, image format) rebuild the tree once their signal is done.
void ExportPresetDialog::rebuildProperties()
{
    m_properties->clear();
    const char* ctx = formatContext(m_preset.format);
    ExportPreset& p = m_preset;
    const auto later = [this](auto change) {
        // A change that alters which rows exist rebuilds after the signal.
        QMetaObject::invokeMethod(this, [this, change] { change(); rebuildProperties(); }, Qt::QueuedConnection);
    };
    // Small builders for the rows: a group with an optional switch that
    // enables its rows, a labelled row, a combo box, a list of translated
    // choices, and number editors.
    const auto group = [&](const char* name, bool* enabled) {
        auto* item = new QTreeWidgetItem(m_properties, {translated(ctx, name)});
        item->setFirstColumnSpanned(false);
        if (enabled) {
            auto* box = new QCheckBox(m_properties);
            box->setObjectName(QStringLiteral("presetExport%1").arg(QString::fromLatin1(name)));
            box->setChecked(*enabled);
            connect(box, &QCheckBox::toggled, this, [this, enabled, item](bool on) {
                *enabled = on;
                for (int r = 0; r < item->childCount(); ++r)
                    if (QWidget* editor = m_properties->itemWidget(item->child(r), 1)) editor->setEnabled(on);
                updateProblems();
            });
            m_properties->setItemWidget(item, 1, box);
        }
        item->setExpanded(true);
        return item;
    };
    const auto row = [&](QTreeWidgetItem* parent, const QString& label, QWidget* editor, const QString& name) {
        auto* item = new QTreeWidgetItem(parent, {label});
        editor->setObjectName(QStringLiteral("presetProperty_%1").arg(name));
        m_properties->setItemWidget(item, 1, editor);
        return item;
    };
    const auto combo = [&](const QStringList& items, int current, std::function<void(int)> apply) {
        auto* box = new QComboBox(m_properties);
        box->addItems(items);
        box->setCurrentIndex(qBound(0, current, items.size() - 1));
        connect(box, &QComboBox::currentIndexChanged, this, [this, apply](int index) { apply(index); updateProblems(); });
        return box;
    };
    const auto names = [&](const char* const* list, int count, const char* context) {
        QStringList items;
        for (int i = 0; i < count; ++i) items << translated(context, list[i]);
        return items;
    };
    // A size or rate with its "Use Source" switch.
    const auto sourced = [&](QWidget* value, bool* useSource) {
        auto* holder = new QWidget(m_properties);
        auto* line = new QHBoxLayout(holder);
        line->setContentsMargins(0, 0, 0, 0);
        line->addWidget(value, 1);
        auto* source = new QCheckBox(QCoreApplication::translate("ExportPresetPropertiesItemModel", "Use Source"), holder);
        source->setObjectName(QStringLiteral("useSource"));
        source->setChecked(*useSource);
        value->setEnabled(!*useSource);
        connect(source, &QCheckBox::toggled, this, [this, value, useSource](bool on) {
            *useSource = on;
            value->setEnabled(!on);
            updateProblems();
        });
        line->addWidget(source);
        return holder;
    };
    const auto integer = [&](int value, int minimum, int maximum, const QString& suffix,
                             std::function<void(int)> apply) {
        auto* spin = new QSpinBox(m_properties);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        spin->setSuffix(suffix);
        connect(spin, &QSpinBox::valueChanged, this, [this, apply](int v) { apply(v); updateProblems(); });
        return spin;
    };
    const auto megabits = [&](int bitsPerSecond, std::function<void(int)> apply) {
        auto* spin = new QDoubleSpinBox(m_properties);
        spin->setRange(0.1, 800.0);
        spin->setDecimals(1);
        spin->setSuffix(QStringLiteral(" Mbps"));
        spin->setValue(bitsPerSecond / 1000000.0);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, apply](double v) {
            apply(int(std::lround(v * 1000000.0)));
            updateProblems();
        });
        return spin;
    };

    const bool movie = p.format != ExportFormat::WAV;
    if (movie) {
        const bool optional = p.format != ExportFormat::ImageSequence;
        QTreeWidgetItem* video = group("Video", optional ? &p.exportVideo : nullptr);
        row(video, translated(ctx, "Width"), sourced(integer(p.width, 16, 16384, QString(), [&p](int v) { p.width = v; }),
                                             &p.widthUseSource), QStringLiteral("width"));
        row(video, translated(ctx, "Height"), sourced(integer(p.height, 16, 16384, QString(), [&p](int v) { p.height = v; }),
                                              &p.heightUseSource), QStringLiteral("height"));
        row(video, translated(ctx, "Scale Mode"),
            combo(names(kScaleModes, 3, ctx), int(p.scaleMode),
                  [&p](int i) { p.scaleMode = ExportPreset::ScaleMode(i); }), QStringLiteral("scaleMode"));
        if (p.format != ExportFormat::ImageSequence) {
            auto* rate = new QDoubleSpinBox(m_properties);
            rate->setRange(1.0, 240.0);
            rate->setDecimals(3);
            rate->setValue(p.frameRate);
            connect(rate, &QDoubleSpinBox::valueChanged, this, [this, &p](double v) { p.frameRate = v; updateProblems(); });
            row(video, translated(ctx, "Frame Rate"), sourced(rate, &p.frameRateUseSource), QStringLiteral("frameRate"));
        }
        const bool h264 = p.format == ExportFormat::MP4
                          || (p.format == ExportFormat::MOV && p.movCodec == ExportPreset::MovCodec::H264);
        if (p.format == ExportFormat::MP4) {
            row(video, translated(ctx, "Codec"), new QLabel(translated(ctx, "H.264"), m_properties), QStringLiteral("codec"));
        } else if (p.format == ExportFormat::MOV) {
            auto* codec = combo(names(kMovCodecs, 9, "MOVExportPreset"), int(p.movCodec), [](int) {});
            connect(codec, &QComboBox::currentIndexChanged, this,
                    [later, &p](int i) { later([&p, i] { p.movCodec = ExportPreset::MovCodec(i); }); });
            row(video, translated(ctx, "Codec"), codec, QStringLiteral("codec"));
        } else if (p.format == ExportFormat::AVI) {
            auto* codec = combo(names(kAviCodecs, 4, "AVIExportPreset"), int(p.aviCodec), [](int) {});
            connect(codec, &QComboBox::currentIndexChanged, this,
                    [later, &p](int i) { later([&p, i] { p.aviCodec = ExportPreset::AviCodec(i); }); });
            row(video, translated(ctx, "Codec"), codec, QStringLiteral("codec"));
        } else {
            auto* format = combo(names(kImageFormats, 4, "ImageSequenceExportPreset"), int(p.imageFormat), [](int) {});
            connect(format, &QComboBox::currentIndexChanged, this,
                    [later, &p](int i) { later([&p, i] { p.imageFormat = ExportPreset::ImageFormat(i); }); });
            row(video, translated(ctx, "Format"), format, QStringLiteral("format"));
        }
        if (h264) {
            row(video, QCoreApplication::translate("MP4ExportPreset", "Profile"),
                combo(names(kProfiles, 3, "MP4ExportPreset"), int(p.h264Profile),
                      [&p](int i) { p.h264Profile = ExportPreset::H264Profile(i); }), QStringLiteral("profile"));
            auto* encoding = combo(names(kEncodings, 2, "MP4ExportPreset"), int(p.bitRateMode), [](int) {});
            connect(encoding, &QComboBox::currentIndexChanged, this,
                    [later, &p](int i) { later([&p, i] { p.bitRateMode = ExportPreset::BitRateMode(i); }); });
            row(video, QCoreApplication::translate("MP4ExportPreset", "Encoding"), encoding, QStringLiteral("encoding"));
            const bool variable = p.bitRateMode == ExportPreset::BitRateMode::Variable;
            row(video, variable ? QCoreApplication::translate("MP4ExportPreset", "Target Bitrate") : QCoreApplication::translate("MP4ExportPreset", "Bitrate"),
                megabits(p.bitRate, [&p](int v) { p.bitRate = v; }), QStringLiteral("bitRate"));
            if (variable) {
                row(video, QCoreApplication::translate("MP4ExportPreset", "Max. Bitrate"),
                    megabits(p.maxBitRate, [&p](int v) { p.maxBitRate = v; }), QStringLiteral("maxBitRate"));
            }
            if (p.format == ExportFormat::MOV) {
                row(video, QCoreApplication::translate("MOVExportPreset", "Key Frame Interval"),
                    integer(p.keyFrameInterval, 0, 600, QStringLiteral(" ") + QCoreApplication::translate("MOVExportPreset", "Frames"),
                            [&p](int v) { p.keyFrameInterval = v; }), QStringLiteral("keyFrameInterval"));
            }
        }
        const bool jpeg = (p.format == ExportFormat::MOV && p.movCodec == ExportPreset::MovCodec::PhotoJpeg)
                          || (p.format == ExportFormat::ImageSequence && p.imageFormat == ExportPreset::ImageFormat::Jpg);
        if (jpeg) {
            row(video, translated(ctx, "Quality"),
                integer(p.quality, 1, 100, QStringLiteral(" %"), [&p](int v) { p.quality = v; }), QStringLiteral("quality"));
        }
        const bool cineForm = (p.format == ExportFormat::MOV && p.movCodec == ExportPreset::MovCodec::CineForm)
                              || (p.format == ExportFormat::AVI && p.aviCodec == ExportPreset::AviCodec::CineForm);
        if (cineForm) {
            row(video, translated(ctx, "Format"),
                combo(names(kCineFormFormats, 2, ctx), p.cineFormRgb ? 1 : 0, [&p](int i) { p.cineFormRgb = i == 1; }),
                QStringLiteral("cineFormFormat"));
        }
        const bool alphaChoice = cineForm
            || (p.format == ExportFormat::MOV && (p.movCodec == ExportPreset::MovCodec::ProRes4444
                                                  || p.movCodec == ExportPreset::MovCodec::ProRes4444XQ))
            || (p.format == ExportFormat::AVI && p.aviCodec == ExportPreset::AviCodec::Uncompressed)
            || (p.format == ExportFormat::ImageSequence && (p.imageFormat == ExportPreset::ImageFormat::Png
                                                            || p.imageFormat == ExportPreset::ImageFormat::OpenExr));
        if (alphaChoice) {
            row(video, translated(ctx, "Channels"),
                combo(names(kChannels, 2, ctx), p.alpha ? 1 : 0, [&p](int i) { p.alpha = i == 1; }),
                QStringLiteral("channels"));
        }
        if (p.format == ExportFormat::ImageSequence) {
            auto* prefix = new QLineEdit(p.prefix, m_properties);
            connect(prefix, &QLineEdit::textChanged, this, [&p](const QString& text) { p.prefix = text; });
            row(video, translated(ctx, "Prefix"), prefix, QStringLiteral("prefix"));
        }
        if (optional) {
            for (int r = 0; r < video->childCount(); ++r)
                if (QWidget* editor = m_properties->itemWidget(video->child(r), 1)) editor->setEnabled(p.exportVideo);
        }
    }
    if (p.format != ExportFormat::ImageSequence) {
        QTreeWidgetItem* audio = group("Audio", p.format == ExportFormat::WAV ? nullptr : &p.exportAudio);
        if (p.format == ExportFormat::MOV) {
            auto* codec = combo(names(kMovAudioCodecs, 2, "MOVExportPreset"), int(p.audioCodec), [](int) {});
            connect(codec, &QComboBox::currentIndexChanged, this,
                    [later, &p](int i) { later([&p, i] { p.audioCodec = ExportPreset::AudioCodec(i); }); });
            row(audio, translated(ctx, "Codec"), codec, QStringLiteral("audioCodec"));
        } else {
            row(audio, translated(ctx, "Codec"),
                new QLabel(p.format == ExportFormat::MP4 ? translated(ctx, "AAC") : translated(ctx, "PCM"), m_properties),
                QStringLiteral("audioCodec"));
        }
        QStringList rates;
        int currentRate = 6;
        for (int i = 0; i < int(std::size(kSampleRates)); ++i) {
            rates << QStringLiteral("%1 Hz").arg(kSampleRates[i]);
            if (kSampleRates[i] == p.audioSampleRate) currentRate = i;
        }
        row(audio, translated(ctx, "Sample Rate"),
            combo(rates, currentRate, [&p](int i) { p.audioSampleRate = kSampleRates[i]; }), QStringLiteral("sampleRate"));
        if (p.audioCodec == ExportPreset::AudioCodec::Aac) {
            QStringList bitRates;
            int currentBitRate = 8;
            for (int i = 0; i < int(std::size(kAacBitRates)); ++i) {
                bitRates << QStringLiteral("%1 Kbps").arg(kAacBitRates[i]);
                if (kAacBitRates[i] * 1000 == p.audioBitRate) currentBitRate = i;
            }
            row(audio, QCoreApplication::translate("MP4ExportPreset", "Bitrate"),
                combo(bitRates, currentBitRate, [&p](int i) { p.audioBitRate = kAacBitRates[i] * 1000; }),
                QStringLiteral("audioBitRate"));
        }
        row(audio, translated(ctx, "Channels"), new QLabel(translated(ctx, "Stereo"), m_properties), QStringLiteral("audioChannels"));
        if (p.format != ExportFormat::WAV) {
            for (int r = 0; r < audio->childCount(); ++r)
                if (QWidget* editor = m_properties->itemWidget(audio->child(r), 1)) editor->setEnabled(p.exportAudio);
        }
    }
    updateProblems();
}

// Shows what the encoder would reject about the preset, and allows OK only
// with a name that is not taken.
void ExportPresetDialog::updateProblems()
{
    // A shot of 1920 x 1080 at 30 fps stands in for the one exported, for
    // the sizes "Use Source" leaves open.
    QStringList problems = render::validateExportPreset(m_preset, QSize(1920, 1080), 30.0);
    const bool taken = m_nameTaken && m_nameTaken(m_preset.name);
    m_problems->setText(problems.join(QLatin1Char('\n')));
    m_problems->setVisible(!problems.isEmpty());
    m_ok->setEnabled(!m_preset.name.isEmpty() && !taken);
}

// --- ExportPresetsPage -----------------------------------------------------

ExportPresetsPage::ExportPresetsPage(ExportPresetManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager)
{
    setObjectName(QStringLiteral("tabPresets"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout;
    auto* defaultLabel = new QLabel(QCoreApplication::translate("ExportPanelWidget", "Default Preset:"), this);
    top->addWidget(defaultLabel);
    m_default = new QComboBox(this);
    m_default->setObjectName(QStringLiteral("comboBoxDefaultPreset"));
    m_default->setToolTip(QCoreApplication::translate("ExportPanelWidget", "The preset to use for new export tasks"));
    top->addWidget(m_default, 1);
    layout->addLayout(top);

    auto* tools = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("lineEditPresetSearch"));
    m_search->setPlaceholderText(QCoreApplication::translate("ExportPanelWidget", "Search in Presets"));
    m_search->setToolTip(QCoreApplication::translate("ExportPanelWidget", "Search for a preset by name"));
    m_search->setClearButtonEnabled(true);
    tools->addWidget(m_search, 1);
    m_add = new QToolButton(this);
    m_add->setObjectName(QStringLiteral("toolButtonAddPreset"));
    m_add->setText(QCoreApplication::translate("ExportPanelWidget", "New Preset"));
    m_add->setToolTip(QCoreApplication::translate("ExportPanelWidget", "Create a new preset"));
    tools->addWidget(m_add);
    m_delete = new QToolButton(this);
    m_delete->setObjectName(QStringLiteral("toolButtonDeletePreset"));
    m_delete->setText(QCoreApplication::translate("ExportTaskActions", "Delete Preset(s)"));
    m_delete->setToolTip(QCoreApplication::translate("ExportPanelWidget", "Delete the selected preset(s)"));
    tools->addWidget(m_delete);
    layout->addLayout(tools);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("treeViewPresets"));
    m_tree->setToolTip(QCoreApplication::translate("ExportPanelWidget",
                          "A list of installed Export Presets. Right click here for additional options"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({QCoreApplication::translate("ExportPresetDialog", "Name"), QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "Format"),
                             QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "Resolution"), QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "Framerate"),
                             QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "Comment")});
    QTreeWidgetItem* header = m_tree->headerItem();
    header->setToolTip(0, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "The name of this preset"));
    header->setToolTip(1, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "The output file format (e.g. MP4) of this preset"));
    header->setToolTip(2, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "The resolution to use when exporting with this preset"));
    header->setToolTip(3, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "The framerate to use when exporting with this preset"));
    header->setToolTip(4, QCoreApplication::translate("ExportPresetDialog", "The user comment for this preset"));
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    connect(m_manager, &ExportPresetManager::presetsChanged, this, &ExportPresetsPage::refresh);
    connect(m_search, &QLineEdit::textChanged, this, &ExportPresetsPage::applySearch);
    connect(m_add, &QToolButton::clicked, this, &ExportPresetsPage::createPreset);
    connect(m_delete, &QToolButton::clicked, this, [this] { deletePresets(selectedPresets()); });
    connect(m_default, &QComboBox::currentIndexChanged, this, [this] {
        if (m_refreshing) return;
        makeDefault(m_default->currentData().toString());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ExportPresetsPage::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString name = item->data(0, Qt::UserRole).toString();
        if (!name.isEmpty()) editPreset(name);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        bool removable = false;
        for (const QString& name : selectedPresets()) removable = removable || m_manager->preset(name).userCreated;
        m_delete->setEnabled(removable);
    });
    refresh();
}

// Lists the presets under Built-In and User, the default in bold, keeping
// the selection, and fills the default preset box.
void ExportPresetsPage::refresh()
{
    m_refreshing = true;
    const QStringList selected = selectedPresets();
    m_tree->clear();
    auto* builtIn = new QTreeWidgetItem(m_tree, {QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "Built-In Presets")});
    builtIn->setToolTip(0, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "These presets are supplied with %1 and cannot be altered.")
                               .arg(QCoreApplication::applicationName()));
    auto* user = new QTreeWidgetItem(m_tree, {QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "User Presets")});
    user->setToolTip(0, QCoreApplication::translate("biff::ui::exporter::ExportPresetItemModel", "These presets may be edited or deleted"));
    builtIn->setFlags(Qt::ItemIsEnabled);
    user->setFlags(Qt::ItemIsEnabled);
    const QString defaultName = m_manager->defaultPreset();
    m_default->clear();
    for (const ExportPreset& preset : m_manager->presets()) {
        auto* item = new QTreeWidgetItem(preset.userCreated ? user : builtIn,
                                         {render::exportPresetDisplayName(preset),
                                          render::exportFormatName(preset.format),
                                          render::exportPresetResolutionText(preset),
                                          render::exportPresetFrameRateText(preset),
                                          preset.userCreated ? preset.comment
                                                             : translated("ExportPreset", preset.comment.toUtf8().constData())});
        item->setData(0, Qt::UserRole, preset.name);
        if (preset.name == defaultName) {
            QFont bold = item->font(0);
            bold.setBold(true);
            item->setFont(0, bold);
        }
        if (selected.contains(preset.name)) item->setSelected(true);
        m_default->addItem(render::exportPresetDisplayName(preset), preset.name);
    }
    m_default->setCurrentIndex(m_default->findData(defaultName));
    builtIn->setExpanded(true);
    user->setExpanded(true);
    applySearch();
    m_refreshing = false;
}

// The names of the selected presets (the group rows have none).
QStringList ExportPresetsPage::selectedPresets() const
{
    QStringList names;
    for (QTreeWidgetItem* item : m_tree->selectedItems()) {
        const QString name = item->data(0, Qt::UserRole).toString();
        if (!name.isEmpty()) names << name;
    }
    return names;
}

// Hides the presets whose name does not contain the search text.
void ExportPresetsPage::applySearch()
{
    const QString text = m_search->text().trimmed();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* item = group->child(i);
            item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive));
        }
    }
}

// Logs a failure and tells the user; nothing for an empty message.
void ExportPresetsPage::report(const QString& message)
{
    if (message.isEmpty()) return;
    OV_LOG_WARN(message);
    QMessageBox box(QMessageBox::Warning, QCoreApplication::applicationName(), message, QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("promptExportPresetError"));
    box.exec();
}

// A new preset starts as a copy of the default one.
void ExportPresetsPage::createPreset()
{
    ExportPreset preset = m_manager->preset(m_manager->defaultPreset());
    if (!preset.isValid()) preset = render::builtInExportPresets().first();
    preset.name = m_manager->uniqueName(QCoreApplication::translate("ExportPreset", "New Preset"));
    preset.comment.clear();
    preset.userCreated = true;
    ExportPresetDialog dialog(preset, true, [this](const QString& name) { return m_manager->contains(name); }, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!m_manager->savePreset(dialog.preset(), QString(), &error)) report(error);
}

// Edits a user preset in place; a built-in one opens as a copy to save.
void ExportPresetsPage::editPreset(const QString& name)
{
    const ExportPreset preset = m_manager->preset(name);
    if (!preset.isValid()) return;
    if (!preset.userCreated) {
        // A built-in preset is not changed; it can be saved as a copy.
        ExportPreset copy = preset;
        copy.name = m_manager->uniqueName(name);
        copy.comment = translated("ExportPreset", preset.comment.toUtf8().constData());
        copy.userCreated = false;
        ExportPresetDialog dialog(copy, false, [this](const QString& n) { return m_manager->contains(n); }, this);
        if (dialog.exec() != QDialog::Accepted) return;
        QString error;
        if (!m_manager->savePreset(dialog.preset(), QString(), &error)) report(error);
        return;
    }
    ExportPresetDialog dialog(preset, false, [this, name](const QString& n) {
        return n != name && m_manager->contains(n);
    }, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!m_manager->savePreset(dialog.preset(), name, &error)) report(error);
}

// Asks for a new name for a user preset.
void ExportPresetsPage::renamePreset(const QString& name)
{
    const ExportPreset preset = m_manager->preset(name);
    if (!preset.isValid() || !preset.userCreated) return;
    bool ok = false;
    const QString title = QCoreApplication::translate("ExportTaskActions", "Rename Preset...").remove(QStringLiteral("..."));
    const QString renamed = QInputDialog::getText(this, title, QCoreApplication::translate("ExportPresetDialog", "Name"),
                                                  QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || renamed.isEmpty() || renamed == name) return;
    ExportPreset changed = preset;
    changed.name = renamed;
    QString error;
    if (!m_manager->savePreset(changed, name, &error)) report(error);
}

void ExportPresetsPage::duplicatePresets(const QStringList& names)
{
    QString error;
    m_manager->duplicatePresets(names, &error);
    report(error);
}

// Deletes the user presets among the names, after asking.
void ExportPresetsPage::deletePresets(const QStringList& names)
{
    QStringList removable;
    for (const QString& name : names)
        if (m_manager->preset(name).userCreated) removable << name;
    if (removable.isEmpty()) return;
    QMessageBox box(QMessageBox::Question, QCoreApplication::applicationName(),
                    QCoreApplication::translate("ExportPresetTreeView",
                       "Are you sure you want to delete the selected preset(s)?\n\nThis cannot be undone."),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setObjectName(QStringLiteral("promptDeleteExportPresets"));
    if (box.exec() != QMessageBox::Yes) return;
    QString error;
    m_manager->removePresets(removable, &error);
    report(error);
}

void ExportPresetsPage::makeDefault(const QString& name)
{
    QString error;
    if (!name.isEmpty() && !m_manager->setDefaultPreset(name, &error)) report(error);
}

// The list's context menu; renaming and deleting apply to user presets only.
void ExportPresetsPage::showContextMenu(const QPoint& position)
{
    QTreeWidgetItem* item = m_tree->itemAt(position);
    const QString name = item ? item->data(0, Qt::UserRole).toString() : QString();
    const QStringList selected = selectedPresets();
    const bool user = !name.isEmpty() && m_manager->preset(name).userCreated;
    QMenu menu(this);
    menu.addAction(QCoreApplication::translate("ExportTaskActions", "Create Preset"), this, &ExportPresetsPage::createPreset);
    if (!name.isEmpty()) {
        menu.addAction(QCoreApplication::translate("ExportTaskActions", "Edit Preset..."), this, [this, name] { editPreset(name); });
        QAction* rename = menu.addAction(QCoreApplication::translate("ExportTaskActions", "Rename Preset..."), this,
                                         [this, name] { renamePreset(name); });
        rename->setEnabled(user);
        menu.addAction(QCoreApplication::translate("ExportTaskActions", "Duplicate Preset(s)"), this,
                       [this, selected] { duplicatePresets(selected); });
        QAction* remove = menu.addAction(QCoreApplication::translate("ExportTaskActions", "Delete Preset(s)"), this,
                                         [this, selected] { deletePresets(selected); });
        remove->setEnabled(user);
        menu.addSeparator();
        menu.addAction(QCoreApplication::translate("ExportTaskActions", "Make Default"), this, [this, name] { makeDefault(name); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

} // namespace ui
} // namespace openvegas
