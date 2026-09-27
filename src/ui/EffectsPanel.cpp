#include "ui/EffectsPanel.h"
#include "ui_Effects.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

EffectsPanel::EffectsPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::EffectsPanel form;
    form.setupUi(this);
    m_search = form.effects_panel_search;
    m_showAll = form.effects_panel_show_all;
    m_tree = form.effectsTree;
    m_itemCount = form.effects_panel_count;
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(m_showAll, &QToolButton::clicked, this, &EffectsPanel::showAll);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 22);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (!item) {
            return;
        }
        const QVariant id = item->data(0, Qt::UserRole);
        if (column == 1 && id.isValid()) {
            toggleFavourite(id.toString());
            return;
        }
        if (const plugin::EffectSpec* spec = specForItem(item)) {
            emit effectSelected(*spec);
        }
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column) {
                if (column == 1) {
                    return; // the favourites star column
                }
                if (const plugin::EffectSpec* spec = specForItem(item)) {
                    emit effectActivated(*spec);
                }
            });

    rebuildTree();
}

const plugin::EffectSpec* EffectsPanel::specForItem(const QTreeWidgetItem* item) const
{
    if (!item) {
        return nullptr;
    }
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) {
        return nullptr; // a category row
    }
    for (const plugin::EffectSpec& spec : m_effects) {
        if (spec.id.value() == id) {
            return &spec;
        }
    }
    return nullptr;
}

void EffectsPanel::applyFilter()
{
    if (!m_tree) {
        return;
    }
    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();
    for (int t = 0; t < m_tree->topLevelItemCount(); ++t) {
        QTreeWidgetItem* root = m_tree->topLevelItem(t);
        if (!root) {
            continue;
        }
        bool rootVisible = filter.isEmpty();
        for (int c = 0; c < root->childCount(); ++c) {
            QTreeWidgetItem* child = root->child(c);
            // Name, category and the plugin's own keywords, so a search for
            // "greenscreen" finds Chroma Key.
            const QString haystack = child->data(0, Qt::UserRole + 1).toString();
            const bool match =
                filter.isEmpty() || child->text(0).toLower().contains(filter)
                || (!haystack.isEmpty() && haystack.contains(filter));
            child->setHidden(!match);
            if (match) {
                rootVisible = true;
            }
        }
        root->setHidden(!rootVisible);
        if (rootVisible) {
            root->setExpanded(true);
        }
    }
    updateItemCount();
}

void EffectsPanel::setEffects(const QVector<plugin::EffectSpec>& effects)
{
    m_effects = effects;
    rebuildTree();
}

void EffectsPanel::showAll()
{
    if (m_search) {
        m_search->clear();
    }
    applyFilter();
}

// One effect row: name in column 0, favourite star in column 1. The reference
// draws a star on every effect line and fills it once the effect is favourited.
// The reference marks each effect in the browser with an icon for the plugin
// family it belongs to - its resources carry effect-2d-plugin,
// effect-geometry-plugin, effect-behavior-plugin, effect-ofx-plugin and
// effect-ae-plugin. Our PluginKind already distinguishes the first three; the
// families this port has no host for (OFX, After Effects) are simply not
// produced yet, and everything else falls back to a generic effect mark.
QIcon EffectsPanel::iconForKind(plugin::PluginKind kind)
{
    QString name;
    switch (kind) {
    case plugin::PluginKind::Effect2D:        name = QStringLiteral("effect-2d"); break;
    case plugin::PluginKind::GeometryEffect:  name = QStringLiteral("effect-geometry"); break;
    case plugin::PluginKind::BehaviorEffect:  name = QStringLiteral("effect-behavior"); break;
    case plugin::PluginKind::AudioEffect:     name = QStringLiteral("effect-audio"); break;
    case plugin::PluginKind::AudioTransition:
    case plugin::PluginKind::VideoTransition:
    case plugin::PluginKind::Transitions:     name = QStringLiteral("effect-transition"); break;
    default:                                  name = QStringLiteral("effect-generic"); break;
    }
    // Cached: the browser rebuilds its whole tree on every filter change, and
    // an SVG re-read per row for hundreds of plugins is wasted work.
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(name));
    cache.insert(name, icon);
    return icon;
}

QTreeWidgetItem* EffectsPanel::addEffectRow(QTreeWidgetItem* parent,
                                            const plugin::EffectSpec& spec)
{
    const QString id = spec.id.value();
    auto* item = new QTreeWidgetItem(parent, {spec.displayName, QString()});
    item->setData(0, Qt::UserRole, id);
    item->setIcon(0, iconForKind(spec.kind));
    // Native plugins declare their own identifier, category and vendor; showing
    // them here is the only place that recovered metadata is visible.
    QStringList lines;
    lines << QStringLiteral("<b>%1</b>").arg(spec.displayName.toHtmlEscaped());
    const QString categoryLine =
        spec.subCategory.isEmpty()
            ? spec.category
            : QStringLiteral("%1 / %2").arg(spec.category, spec.subCategory);
    lines << QStringLiteral("%1 &middot; %2").arg(categoryLine.toHtmlEscaped(),
                                                 spec.kindName().toHtmlEscaped());
    if (id != spec.displayName) {
        lines << QStringLiteral("<code>%1</code>").arg(id.toHtmlEscaped());
    }
    if (!spec.description.isEmpty()) {
        lines << spec.description.toHtmlEscaped();
    }
    if (!spec.renderable && !spec.unavailableReason.isEmpty()) {
        lines << QStringLiteral("<i>%1</i>").arg(spec.unavailableReason.toHtmlEscaped());
    }
    item->setToolTip(0, lines.join(QStringLiteral("<br>")));
    // The search matches the keywords a plugin carries in braces after its name
    // as well as the name itself - "greenscreen" has to find Chroma Key, which
    // is the whole point of the reference shipping them.
    QStringList haystack;
    haystack << spec.displayName << spec.category << spec.subCategory << spec.keywords;
    item->setData(0, Qt::UserRole + 1, haystack.join(QLatin1Char(' ')).toLower());
    // An effect that cannot run is drawn as unavailable rather than looking like
    // any other row that does nothing when clicked.
    if (!spec.renderable) {
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
        item->setForeground(0, QBrush(QColor(150, 150, 155)));
    }
    const bool starred = m_favourites.contains(id);
    // U+2605 BLACK STAR / U+2606 WHITE STAR, spelled numerically so the source
    // stays pure ASCII for MSVC.
    item->setText(1, QString(starred ? QChar(0x2605) : QChar(0x2606)));
    item->setToolTip(1, starred ? tr("Remove from Favorites") : tr("Add to Favorites"));
    item->setTextAlignment(1, Qt::AlignCenter);
    return item;
}

void EffectsPanel::rebuildTree()
{
    if (!m_tree) {
        return;
    }
    m_tree->clear();

    // Reference order: Favorites first, then the effect categories.
    if (!m_favourites.isEmpty()) {
        auto* favouritesRoot = new QTreeWidgetItem(m_tree, {tr("Favorites")});
        favouritesRoot->setFlags(Qt::ItemIsEnabled);
        m_tree->addTopLevelItem(favouritesRoot);
        for (const plugin::EffectSpec& spec : m_effects) {
            if (m_favourites.contains(spec.id.value())) {
                favouritesRoot->addChild(addEffectRow(favouritesRoot, spec));
            }
        }
    }

    QHash<QString, QTreeWidgetItem*> categoryRoots;
    for (const plugin::EffectSpec& spec : m_effects) {
        QTreeWidgetItem* root = categoryRoots.value(spec.category);
        if (!root) {
            root = new QTreeWidgetItem(m_tree, {spec.category});
            root->setFlags(Qt::ItemIsEnabled);
            categoryRoots.insert(spec.category, root);
            m_tree->addTopLevelItem(root);
        }
        root->addChild(addEffectRow(root, spec));
    }

    m_tree->sortItems(0, Qt::AscendingOrder);
    m_tree->expandAll();
    updateItemCount();
}

void EffectsPanel::toggleFavourite(const QString& effectId)
{
    if (effectId.isEmpty()) {
        return;
    }
    if (m_favourites.contains(effectId)) {
        m_favourites.remove(effectId);
    } else {
        m_favourites.insert(effectId);
    }
    rebuildTree();
    applyFilter();
}

QStringList EffectsPanel::favourites() const
{
    QStringList ids(m_favourites.cbegin(), m_favourites.cend());
    ids.sort();
    return ids;
}

void EffectsPanel::setFavourites(const QStringList& ids)
{
    m_favourites = QSet<QString>(ids.cbegin(), ids.cend());
    rebuildTree();
}

// Footer counter, matching the reference "424 item(s)" readout. Counts the
// effect rows currently visible, so it tracks the search filter.
void EffectsPanel::updateItemCount()
{
    if (!m_itemCount || !m_tree) {
        return;
    }
    int visible = 0;
    for (int t = 0; t < m_tree->topLevelItemCount(); ++t) {
        QTreeWidgetItem* root = m_tree->topLevelItem(t);
        if (!root || root->isHidden()) {
            continue;
        }
        for (int c = 0; c < root->childCount(); ++c) {
            if (!root->child(c)->isHidden()) {
                ++visible;
            }
        }
    }
    m_itemCount->setText(tr("%n item(s)", "", visible));
}

} // namespace ui
} // namespace openvegas
