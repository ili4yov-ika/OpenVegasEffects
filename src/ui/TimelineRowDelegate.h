#pragma once
#include <QStyledItemDelegate>
#include <QPainter>
namespace openvegas::ui {
class TimelineRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto row = index.siblingAtColumn(0);
        if (!row.data(Qt::UserRole + 3).isValid() && !row.data(Qt::UserRole + 4).isValid()) {
            QStyledItemDelegate::paint(painter, option, index); return;
        }
        painter->save(); painter->setClipRect(option.rect);
        painter->fillRect(option.rect, option.state & QStyle::State_Selected ? QColor("#566873") : QColor("#333333"));
        painter->setPen(QColor("#242424")); painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        if (index.column() == 0) {
            const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            icon.paint(painter, QRect(option.rect.left() + 2, option.rect.top() + 4, 14, 14));
            painter->setPen(QColor("#d1d1d1")); painter->setFont(option.font);
            const QRect label = option.rect.adjusted(icon.isNull() ? 4 : 21, 0, -4, 0);
            painter->drawText(label, Qt::AlignVCenter | Qt::AlignLeft, option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, label.width()));
        }
        painter->restore();
    }
};
}
