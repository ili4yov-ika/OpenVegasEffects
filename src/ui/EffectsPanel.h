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

class EffectsFavorites;

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

    // The favourite and recent effects, kept as the reference keeps them
    // (EffectsFavorites); the tree shows them first, Favorites then Recents.
    // Without one the favourites are only this panel's.
    void setFavoritesManager(EffectsFavorites* favorites);
    QStringList favourites() const;
    void setFavourites(const QStringList& ids);
    QStringList recents() const;

signals:
    // Browsing the tree - one click just previews the entry.
    void effectSelected(const plugin::EffectSpec& spec);
    // Committing to it: double-click applies the effect to the current clip.
    void effectActivated(const plugin::EffectSpec& spec);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void rebuildTree();
    void applyFilter();
    void updateItemCount();
    void toggleFavourite(const QString& effectId);
    // The reference's EffectsActions menu (VegasEffects.exe 140681870), the
    // entries for effects: Add to / Remove from Favorites, Clear Recents,
    // Collapse All. The preset entries need effect presets.
    void showContextMenu(const QPoint& position);
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
    EffectsFavorites* m_favoritesManager = nullptr;
    QPoint m_dragStart; // press position in the tree viewport
};

} // namespace ui
} // namespace openvegas