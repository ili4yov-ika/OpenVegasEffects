#pragma once

#include <QWidget>

#include <QString>

namespace openvegas {
namespace ui {

namespace Ui {
class ExportPanel;
}

// Mirrors the reference "Export" panel (type 2057): configure an output
// directory + preset and render the current frame / contents to a file.
// Emits exportFrameRequested(path) with the current directory & format applied.
class ExportPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExportPanel(QWidget* parent = nullptr);
    ~ExportPanel() override;

    QString exportDirectory() const;
    void setExportDirectory(const QString& path);
    void setOutputName(const QString& name) { m_outputName = name; }

    QString currentPreset() const;
    QString outputPath() const;

public slots:
    void exportFrame();

signals:
    void exportFrameRequested(const QString& filePath);
    void exportContentsRequested(const QString& filePath);

private slots:
    void chooseDirectory();

private:
    QString extensionForPreset() const;
    QString buildOutputPath(const QString& extension) const;

    Ui::ExportPanel* m_ui = nullptr;
    QString m_outputName = QStringLiteral("frame");
};

} // namespace ui
} // namespace openvegas
