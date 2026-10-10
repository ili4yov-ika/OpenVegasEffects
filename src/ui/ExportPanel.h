#pragma once

#include <QWidget>

#include <QString>

#include "render/ExportPreset.h"

class QTabWidget;

namespace openvegas {
namespace ui {

class ExportPresetManager;
class ExportPresetsPage;

namespace Ui {
class ExportPanel;
}

// Mirrors the reference "Export" panel (type 2057): configure an output
// directory + preset and render the current frame / contents to a file.
// Emits exportFrameRequested(path) with the current directory & format applied.
// Two tabs as in the reference's ExportPanelWidget: Queue (the export and its
// queue) and Presets (ExportPresetsPage), once a preset manager is set.
class ExportPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExportPanel(QWidget* parent = nullptr);
    ~ExportPanel() override;

    QString exportDirectory() const;
    void setExportDirectory(const QString& path);
    void setOutputName(const QString& name) { m_outputName = name; }

    // The presets the Preset list offers; the default one is chosen.
    void setPresetManager(ExportPresetManager* manager);
    ExportPresetsPage* presetsPage() const { return m_presetsPage; }
    // The chosen preset's name, and the preset (not valid without a manager
    // or for a name it does not know).
    QString currentPreset() const;
    render::ExportPreset currentExportPreset() const;
    QString outputPath() const;
    // The path a movie or sequence export of the contents goes to.
    QString contentsOutputPath() const { return buildOutputPath(extensionForPreset()); }
    // The export queue's view, shown above the progress log.
    void setQueueWidget(QWidget* widget);

public slots:
    void exportFrame();

signals:
    void exportFrameRequested(const QString& filePath);
    void exportContentsRequested(const QString& filePath);
    // ExportTimelineMenu's "Add to Queue": the contents, with this preset.
    void addToQueueRequested(const QString& filePath);

private slots:
    void chooseDirectory();

private:
    QString extensionForPreset() const;
    QString buildOutputPath(const QString& extension) const;

    void fillPresets();

    Ui::ExportPanel* m_ui = nullptr;
    QString m_outputName = QStringLiteral("frame");
    QTabWidget* m_tabs = nullptr;
    ExportPresetManager* m_presets = nullptr;
    ExportPresetsPage* m_presetsPage = nullptr;
};

} // namespace ui
} // namespace openvegas
