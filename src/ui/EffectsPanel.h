#pragma once

#include <QDockWidget>
#include <QIcon>
#include <QSet>
#include <QString>
#include <QTreeWidgetItem>
#include <QVector>

#include "plugin/EffectSpec.h"

class QLabel;
class QLineEdit;
class QToolButton;

namespace openvegas {
namespace ui {

// Mirrors the reference "Effects" panel (type 4). Layout follows the reference
// screenshot: a "Search in Effects" field, a "Show All" row, a headerless
// category tree whose rows carry a favourite star, and an "N item(s)" footer.
class EffectsPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit EffectsPanel(QWidget* parent = nullptr);

    void setEffects(const QVector<plugin::EffectSpec>& effects);
    void showAll();

    // Favourites are held in memory only; the reference persists them, but the
    // screenshots show no storage detail to mirror.
    QStringList favourites() const;
    void setFavourites(const QStringList& ids);

signals:
    // Browsing the tree - one click just previews the entry.
    void effectSelected(const plugin::EffectSpec& spec);
    // Committing to it: double-click applies the effect to the current clip.
    void effectActivated(const plugin::EffectSpec& spec);

private:
    void rebuildTree();
    void applyFilter();
    void updateItemCount();
    void toggleFavourite(const QString& effectId);
    // Icon standing for the plugin family, as the reference marks its rows.
    static QIcon iconForKind(plugin::PluginKind kind);
    QTreeWidgetItem* addEffectRow(QTreeWidgetItem* parent, const plugin::EffectSpec& spec);
    const plugin::EffectSpec* specForItem(const QTreeWidgetItem* item) const;

    QLineEdit* m_search = nullptr;
    QToolButton* m_showAll = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_itemCount = nullptr;
    QVector<plugin::EffectSpec> m_effects;
    QSet<QString> m_favourites;
};

} // namespace ui
} // namespace openvegas