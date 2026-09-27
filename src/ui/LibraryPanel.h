#pragma once

#include <QDockWidget>

class QListWidget;
class QListWidgetItem;
class QLabel;

namespace openvegas {
namespace ui {

// Mirrors the reference "Library" panel (type 2059): browsable library of
// presets/templates. Class name from reference RTTI: LibraryPanel.
// Emits presetActivated() when the user picks an entry to apply to a clip.
class LibraryPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit LibraryPanel(QWidget* parent = nullptr);

    void setLibraries(const QStringList& names);
    void addPreset(const QString& library, const QString& preset);
    void setLibraryPaths(const QString& mediaPath, const QString& templatePath);
    void reload();

signals:
    void presetActivated(const QString& library, const QString& preset);

private:
    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    QString m_mediaPath;
    QString m_templatePath;
};

} // namespace ui
} // namespace openvegas
