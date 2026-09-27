#include "ui/TrackPanel.h"
#include "ui_Track.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

namespace openvegas::ui {
namespace {
const QStringList& blendModes()
{
    static const QStringList list = composition::blendModeNames();
    return list;
}
}

TrackPanel::TrackPanel(QWidget* parent) : QDockWidget(parent)
{
    Ui::TrackPanel form;
    form.setupUi(this);
    m_tree = form.composition_layers_header;
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c : {1, 2, 4}) m_tree->header()->setSectionResizeMode(c, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(1, 48); m_tree->setColumnWidth(2, 48); m_tree->setColumnWidth(4, 68);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
                if (item) onItemChanged(item->data(0, Qt::UserRole).toInt(), column);
            });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) {
                if (!m_updating) emit layerSelected(current ? current->data(0, Qt::UserRole).toInt() : -1);
            });
    auto* add = form.toolButtonNewLayer;
    auto* menu = new QMenu(add);
    for (composition::LayerKind kind : {composition::LayerKind::Point, composition::LayerKind::Text,
             composition::LayerKind::Grade, composition::LayerKind::Light,
             composition::LayerKind::Camera, composition::LayerKind::Plane}) {
        QAction* action = menu->addAction(QCoreApplication::translate(
            "LayerKind", composition::layerKindName(kind)));
        connect(action, &QAction::triggered, this, [this, kind] { emit newLayerRequested(kind); });
    }
    add->setMenu(menu); add->setPopupMode(QToolButton::InstantPopup);
    auto* up = form.toolButtonMoveLayerUp;
    auto* down = form.toolButtonMoveLayerDown;
    auto* del = form.toolButtonRemoveLayer;
    connect(up, &QToolButton::clicked, this, [this] { onMoveLayer(-1); });
    connect(down, &QToolButton::clicked, this, [this] { onMoveLayer(1); });
    connect(del, &QToolButton::clicked, this, &TrackPanel::onDeleteLayer);
}

void TrackPanel::bindModel(std::shared_ptr<composition::Composition> composition)
{ m_composition = std::move(composition); refresh(); }

void TrackPanel::refresh()
{
    if (!m_tree) return;
    const int selected = selectedRow();
    m_updating = true; m_tree->clear();
    if (m_composition) {
        const auto& layers = m_composition->layers();
        for (int i = 0; i < layers.size(); ++i) {
            const auto& layer = layers.at(i);
            auto* item = new QTreeWidgetItem(m_tree);
            item->setText(0, layer.name); item->setData(0, Qt::UserRole, i);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            item->setCheckState(1, layer.visible ? Qt::Checked : Qt::Unchecked);
            item->setCheckState(2, layer.muted ? Qt::Checked : Qt::Unchecked);
            item->setText(4, QStringLiteral("%1%").arg(qRound(layer.opacity * 100.0)));
            auto* blend = new QComboBox(m_tree); blend->addItems(blendModes());
            blend->setCurrentText(layer.blendMode);
            connect(blend, &QComboBox::currentTextChanged, this, [this, i] { onItemChanged(i, 3); });
            m_tree->setItemWidget(item, 3, blend);
        }
    }
    m_updating = false;
    applyFilter(m_filter);
    setSelectedLayer(selected);
}

void TrackPanel::applyFilter(const QString& filter)
{
    m_filter = filter;
    const QString needle = filter.trimmed();
    for (int i = 0; m_tree && i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        item->setHidden(!needle.isEmpty() && !item->text(0).contains(needle, Qt::CaseInsensitive));
    }
}

void TrackPanel::setSelectedLayer(int row)
{
    if (!m_tree) return;
    m_updating = true;
    m_tree->setCurrentItem(row >= 0 && row < m_tree->topLevelItemCount()
                               ? m_tree->topLevelItem(row) : nullptr);
    m_updating = false;
}

int TrackPanel::selectedRow() const
{
    auto* item = m_tree ? m_tree->currentItem() : nullptr;
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

void TrackPanel::onDeleteLayer()
{
    const int row = selectedRow();
    if (!m_composition || row < 0 || m_composition->layers().at(row).locked) return;
    if (m_composition->removeLayer(row)) { refresh(); emitModified(); }
}

void TrackPanel::onMoveLayer(int delta)
{
    const int row = selectedRow(), target = row + delta;
    if (!m_composition || row < 0 || target < 0 || target >= m_composition->layers().size()) return;
    if (m_composition->swapLayers(row, target)) { refresh(); setSelectedLayer(target); emitModified(); }
}

void TrackPanel::onItemChanged(int row, int column)
{
    if (m_updating || !m_composition || row < 0 || row >= m_composition->layers().size()) return;
    auto* item = m_tree->topLevelItem(row); if (!item) return;
    auto& layer = m_composition->layerRef(row);
    switch (column) {
    case 0: layer.name = item->text(0).trimmed(); break;
    case 1: layer.visible = item->checkState(1) == Qt::Checked; break;
    case 2: layer.muted = item->checkState(2) == Qt::Checked; break;
    case 3:
        if (auto* combo = qobject_cast<QComboBox*>(m_tree->itemWidget(item, 3)))
            layer.blendMode = combo->currentText();
        break;
    case 4: {
        QString value = item->text(4).trimmed(); value.remove(QLatin1Char('%'));
        bool ok = false; const double percent = value.toDouble(&ok);
        if (ok) layer.opacity = qBound(0.0, percent / 100.0, 1.0);
        refresh(); setSelectedLayer(row);
        break;
    }
    default: return;
    }
    emitModified();
}

void TrackPanel::emitModified() { emit layersModified(); }
} // namespace openvegas::ui
