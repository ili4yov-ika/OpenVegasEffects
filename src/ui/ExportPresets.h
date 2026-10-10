#pragma once

#include <QDialog>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

#include "render/ExportPreset.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openvegas {
namespace ui {

// The export presets (the reference's biff::ui::exporter::ExportPresetManager):
// the built-in ones and the user's, one "<name>.hfxp" each in the
// ExportPresets folder, and the default for new export tasks
// (Options/DefaultExportPreset). Built-in presets cannot be changed; a
// user preset is saved, renamed, duplicated and removed through it.
class ExportPresetManager : public QObject
{
    Q_OBJECT

public:
    explicit ExportPresetManager(const QString& userDirectory, QObject* parent = nullptr);

    QString userDirectory() const { return m_directory; }
    // Reads the user's presets again; problems are logged.
    void reload();
    // Built-in presets first, then the user's by name.
    const QVector<render::ExportPreset>& presets() const { return m_presets; }
    // The preset of that name; not valid when there is none.
    render::ExportPreset preset(const QString& name) const;
    bool contains(const QString& name) const { return preset(name).isValid(); }

    QString defaultPreset() const;
    bool setDefaultPreset(const QString& name, QString* error = nullptr);

    // Saves a user preset, under a new name when `previousName` is another
    // one (the old file goes).
    bool savePreset(render::ExportPreset preset, const QString& previousName = QString(),
                    QString* error = nullptr);
    bool removePresets(const QStringList& names, QString* error = nullptr);
    // User copies of the presets, named "<name> 1", "<name> 2", ...
    QStringList duplicatePresets(const QStringList& names, QString* error = nullptr);
    // `base`, or "<base> N" when that is taken.
    QString uniqueName(const QString& base) const;

signals:
    void presetsChanged();

private:
    QString pathFor(const QString& name) const;

    QString m_directory;
    QVector<render::ExportPreset> m_presets;
};

// Edit Preset (the reference's ExportPresetDialog): the name, the format of
// a new preset, a comment and the properties of its format, in a
// Property/Value tree; problems the preset has are listed under it.
class ExportPresetDialog : public QDialog
{
    Q_OBJECT

public:
    // `nameTaken` says whether a name is used by another preset.
    ExportPresetDialog(const render::ExportPreset& preset, bool creating,
                       std::function<bool(const QString&)> nameTaken, QWidget* parent = nullptr);

    render::ExportPreset preset() const { return m_preset; }

private:
    void rebuildProperties();
    void updateProblems();

    render::ExportPreset m_preset;
    std::function<bool(const QString&)> m_nameTaken;
    QLineEdit* m_name = nullptr;
    QComboBox* m_format = nullptr;
    QLineEdit* m_comment = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeWidget* m_properties = nullptr;
    QLabel* m_problems = nullptr;
    QPushButton* m_ok = nullptr;
};

// The Export panel's Presets tab (the reference's ExportPanelWidget page):
// the default preset, a search, New and Delete, and the list of built-in
// and user presets - Name, Format, Resolution, Framerate, Comment - with
// the reference's context menu (Create, Edit, Rename, Duplicate, Delete,
// Make Default).
class ExportPresetsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ExportPresetsPage(ExportPresetManager* manager, QWidget* parent = nullptr);

    void refresh();
    QStringList selectedPresets() const;
    QTreeWidget* tree() const { return m_tree; }

    // The actions, also reachable from the context menu and the buttons.
    void createPreset();
    void editPreset(const QString& name);
    void renamePreset(const QString& name);
    void duplicatePresets(const QStringList& names);
    // Asks first, as the reference does.
    void deletePresets(const QStringList& names);
    void makeDefault(const QString& name);

private:
    void showContextMenu(const QPoint& position);
    void applySearch();
    void report(const QString& message);

    ExportPresetManager* m_manager = nullptr;
    QComboBox* m_default = nullptr;
    QLineEdit* m_search = nullptr;
    QToolButton* m_add = nullptr;
    QToolButton* m_delete = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool m_refreshing = false;
};

} // namespace ui
} // namespace openvegas
