#include "ui/LayoutPanel.h"
#include "ui_Layout.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <numeric>

namespace openvegas {
namespace ui {

namespace {

const QColor kGlyph(206, 206, 210);
const QColor kGlyphRule(120, 170, 230);   // the edge an align/distribute acts on

// The reference names each button's icon ("align-left", "distribute-top",
// "mirror-vertical", ...) but the artwork is its own; this port has no such
// files, so the glyphs are drawn here rather than shipping look-alike assets.
// Every one is composed of the same parts the originals are: a coloured rule
// for the edge being aligned to and grey bars for the objects moved onto it.
QIcon makeGlyph(const QString& name)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);

    const auto rule = [&](bool vertical, int at) {
        p.fillRect(vertical ? QRect(at, 1, 1, 14) : QRect(1, at, 14, 1), kGlyphRule);
    };
    const auto bar = [&](const QRect& r) { p.fillRect(r, kGlyph); };

    if (name == QLatin1String("align-left")) {
        rule(true, 1);
        bar(QRect(2, 3, 11, 4));
        bar(QRect(2, 9, 7, 4));
    } else if (name == QLatin1String("align-horizontally")) {
        rule(true, 8);
        bar(QRect(3, 3, 11, 4));
        bar(QRect(5, 9, 7, 4));
    } else if (name == QLatin1String("align-right")) {
        rule(true, 14);
        bar(QRect(3, 3, 11, 4));
        bar(QRect(7, 9, 7, 4));
    } else if (name == QLatin1String("align-top")) {
        rule(false, 1);
        bar(QRect(3, 2, 4, 11));
        bar(QRect(9, 2, 4, 7));
    } else if (name == QLatin1String("align-vertically")) {
        rule(false, 8);
        bar(QRect(3, 3, 4, 11));
        bar(QRect(9, 5, 4, 7));
    } else if (name == QLatin1String("align-bottom")) {
        rule(false, 14);
        bar(QRect(3, 3, 4, 11));
        bar(QRect(9, 7, 4, 7));
    } else if (name == QLatin1String("distribute-left")) {
        bar(QRect(1, 3, 3, 10));
        bar(QRect(7, 3, 3, 10));
        bar(QRect(12, 3, 3, 10));
        rule(true, 1);
        rule(true, 7);
        rule(true, 12);
    } else if (name == QLatin1String("distribute-horizontally")) {
        bar(QRect(1, 3, 3, 10));
        bar(QRect(6, 3, 3, 10));
        bar(QRect(12, 3, 3, 10));
        rule(true, 2);
        rule(true, 7);
        rule(true, 13);
    } else if (name == QLatin1String("distribute-right")) {
        bar(QRect(1, 3, 3, 10));
        bar(QRect(6, 3, 3, 10));
        bar(QRect(12, 3, 3, 10));
        rule(true, 3);
        rule(true, 8);
        rule(true, 14);
    } else if (name == QLatin1String("distribute-top")) {
        bar(QRect(3, 1, 10, 3));
        bar(QRect(3, 7, 10, 3));
        bar(QRect(3, 12, 10, 3));
        rule(false, 1);
        rule(false, 7);
        rule(false, 12);
    } else if (name == QLatin1String("distribute-vertically")) {
        bar(QRect(3, 1, 10, 3));
        bar(QRect(3, 6, 10, 3));
        bar(QRect(3, 12, 10, 3));
        rule(false, 2);
        rule(false, 7);
        rule(false, 13);
    } else if (name == QLatin1String("distribute-bottom")) {
        bar(QRect(3, 1, 10, 3));
        bar(QRect(3, 6, 10, 3));
        bar(QRect(3, 12, 10, 3));
        rule(false, 3);
        rule(false, 8);
        rule(false, 14);
    } else if (name == QLatin1String("mirror-horizontal")) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF left;
        left << QPointF(6.5, 2) << QPointF(6.5, 14) << QPointF(1, 8);
        QPolygonF right;
        right << QPointF(9.5, 2) << QPointF(9.5, 14) << QPointF(15, 8);
        p.setBrush(kGlyph);
        p.drawPolygon(left);
        p.setBrush(QColor(kGlyph.red() / 2, kGlyph.green() / 2, kGlyph.blue() / 2));
        p.drawPolygon(right);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kGlyphRule, 1, Qt::DashLine));
        p.drawLine(QPointF(8, 1), QPointF(8, 15));
    } else if (name == QLatin1String("mirror-vertical")) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF top;
        top << QPointF(2, 6.5) << QPointF(14, 6.5) << QPointF(8, 1);
        QPolygonF bottom;
        bottom << QPointF(2, 9.5) << QPointF(14, 9.5) << QPointF(8, 15);
        p.setBrush(kGlyph);
        p.drawPolygon(top);
        p.setBrush(QColor(kGlyph.red() / 2, kGlyph.green() / 2, kGlyph.blue() / 2));
        p.drawPolygon(bottom);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kGlyphRule, 1, Qt::DashLine));
        p.drawLine(QPointF(1, 8), QPointF(15, 8));
    } else if (name == QLatin1String("scale-linked")) {
        // Two links of a chain, which is what the reference draws beside Width
        // and Height.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kGlyph, 1.6));
        p.drawRoundedRect(QRectF(4.5, 1.5, 7, 7), 3.0, 3.0);
        p.drawRoundedRect(QRectF(4.5, 7.5, 7, 7), 3.0, 3.0);
    } else if (name == QLatin1String("rotate-cw") || name == QLatin1String("rotate-ccw")) {
        const bool clockwise = name.endsWith(QLatin1String("cw"))
                               && !name.endsWith(QLatin1String("ccw"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kGlyph, 2));
        // Three quarters of a circle, open where the arrow head goes.
        p.drawArc(QRectF(3, 3, 10, 10), clockwise ? 90 * 16 : 90 * 16,
                  clockwise ? -260 * 16 : 260 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(kGlyph);
        QPolygonF head;
        if (clockwise) {
            head << QPointF(8, 0.5) << QPointF(12, 3.5) << QPointF(8, 6.5);
        } else {
            head << QPointF(8, 0.5) << QPointF(4, 3.5) << QPointF(8, 6.5);
        }
        p.drawPolygon(head);
    }

    p.end();
    return QIcon(pixmap);
}

QToolButton* makeIconButton(QWidget* parent, const QString& objectName, const QString& iconName,
                            const QString& toolTip)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(makeGlyph(iconName));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFixedSize(22, 22);
    // The reference carries the icon id as a property on the button; kept so
    // the artwork can be swapped for real assets without touching the wiring.
    button->setProperty("icon-image", iconName);
    return button;
}

} // namespace

LayoutPanel::LayoutPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::LayoutPanel form;
    form.setupUi(this);
    m_x = form.spinBoxX;
    m_y = form.spinBoxY;
    m_width = form.spinBoxWidth;
    m_height = form.spinBoxHeight;
    m_scaleLinked = form.toolButtonScaleLinked;
    m_alignTo = form.comboBoxAlignTo;
    m_alignRow = form.widgetAlignment;
    m_distributeRow = form.widgetDistribute;

    const auto setGlyph = [](QToolButton* button, const QString& icon) {
        button->setIcon(makeGlyph(icon));
        button->setProperty("icon-image", icon);
    };
    setGlyph(form.toolButtonMirrorVertical, QStringLiteral("mirror-vertical"));
    setGlyph(form.toolButtonMirrorHorizontal, QStringLiteral("mirror-horizontal"));
    setGlyph(form.toolButtonCounterClockWise, QStringLiteral("rotate-ccw"));
    setGlyph(form.toolButtonClockWise, QStringLiteral("rotate-cw"));
    setGlyph(m_scaleLinked, QStringLiteral("scale-linked"));
    connect(form.toolButtonMirrorVertical, &QToolButton::clicked, this,
            [this] { emit mirrorRequested(Qt::Vertical); });
    connect(form.toolButtonMirrorHorizontal, &QToolButton::clicked, this,
            [this] { emit mirrorRequested(Qt::Horizontal); });
    connect(form.toolButtonCounterClockWise, &QToolButton::clicked, this,
            [this] { emit rotateRequested(-90); });
    connect(form.toolButtonClockWise, &QToolButton::clicked, this,
            [this] { emit rotateRequested(90); });

    QToolButton* directionButtons[] = {
        form.directionTopLeft, form.directionTop, form.directionTopRight,
        form.directionLeft, form.directionCenter, form.directionRight,
        form.directionBottomLeft, form.directionBottom, form.directionBottomRight,
    };
    const Direction directions[] = {
        Direction::TopLeft, Direction::Top, Direction::TopRight,
        Direction::Left, Direction::Center, Direction::Right,
        Direction::BottomLeft, Direction::Bottom, Direction::BottomRight,
    };
    auto* directionGroup = new QButtonGroup(form.widgetDirection);
    directionGroup->setExclusive(true);
    form.widgetDirection->setStyleSheet(QStringLiteral(
        "QToolButton { border:1px solid palette(mid); background:palette(base); }"
        "QToolButton:checked { border:1px solid palette(highlight); background:palette(highlight); }"
        "QToolButton:hover { border:1px solid palette(highlight); }"));
    for (int i = 0; i < 9; ++i) {
        directionButtons[i]->setProperty("direction", int(directions[i]));
        directionButtons[i]->setChecked(directions[i] == m_direction);
        directionGroup->addButton(directionButtons[i]);
        m_directionButtons.append(directionButtons[i]);
        connect(directionButtons[i], &QToolButton::clicked, this,
                [this, direction = directions[i]] { setDirection(direction); });
    }

    m_alignTo->setItemData(0, int(AlignTo::Selection));
    m_alignTo->setItemData(1, int(AlignTo::Timeline));
    m_alignTo->setCurrentIndex(1);
    connect(m_alignTo, &QComboBox::currentIndexChanged, this, [this](int) { updateEnabled(); });
    const struct { QToolButton* button; const char* icon; Qt::Alignment alignment; } aligns[] = {
        {form.toolButtonAlignHorizontalLeft, "align-left", Qt::AlignLeft},
        {form.toolButtonAlignHorizontalCenter, "align-horizontally", Qt::AlignHCenter},
        {form.toolButtonAlignHorizontalRight, "align-right", Qt::AlignRight},
        {form.toolButtonAlignVerticalTop, "align-top", Qt::AlignTop},
        {form.toolButtonAlignVerticalMiddle, "align-vertically", Qt::AlignVCenter},
        {form.toolButtonAlignVerticalBottom, "align-bottom", Qt::AlignBottom},
    };
    for (const auto& entry : aligns) {
        setGlyph(entry.button, QString::fromLatin1(entry.icon));
        connect(entry.button, &QToolButton::clicked, this,
                [this, alignment = entry.alignment] { applyAlignment(alignment); });
    }
    const struct { QToolButton* button; const char* mode; } distributes[] = {
        {form.toolButtonDistributeTop, "distribute-top"},
        {form.toolButtonDistributeVertically, "distribute-vertically"},
        {form.toolButtonDistributeBottom, "distribute-bottom"},
        {form.toolButtonDistributeLeft, "distribute-left"},
        {form.toolButtonDistributeHorizontally, "distribute-horizontally"},
        {form.toolButtonDistributeRight, "distribute-right"},
    };
    for (const auto& entry : distributes) {
        setGlyph(entry.button, QString::fromLatin1(entry.mode));
        connect(entry.button, &QToolButton::clicked, this,
                [this, mode = QString::fromLatin1(entry.mode)] { applyDistribution(mode); });
    }
    connect(m_scaleLinked, &QToolButton::toggled, this, [this](bool on) {
        if (on && m_height->value() > 0.0) m_linkedAspect = m_width->value() / m_height->value();
    });
    const auto onEdited = [this](QDoubleSpinBox* source) {
        if (m_updating || !m_hasSelection) return;
        if (m_scaleLinked->isChecked() && (source == m_width || source == m_height)
            && m_linkedAspect > 0.0) {
            QSignalBlocker blockW(m_width), blockH(m_height);
            if (source == m_width) m_height->setValue(m_width->value() / m_linkedAspect);
            else m_width->setValue(m_height->value() * m_linkedAspect);
        }
        emitBounds(boundsFromFields());
    };
    connect(m_x, &QDoubleSpinBox::editingFinished, this, [this, onEdited] { onEdited(m_x); });
    connect(m_y, &QDoubleSpinBox::editingFinished, this, [this, onEdited] { onEdited(m_y); });
    connect(m_width, &QDoubleSpinBox::editingFinished, this, [this, onEdited] { onEdited(m_width); });
    connect(m_height, &QDoubleSpinBox::editingFinished, this, [this, onEdited] { onEdited(m_height); });

    clearSelection();
}

void LayoutPanel::setDirection(Direction direction)
{
    if (m_direction == direction) {
        return;
    }
    m_direction = direction;
    for (QToolButton* handle : m_directionButtons) {
        handle->setChecked(handle->property("direction").toInt() == static_cast<int>(direction));
    }
    // The box has not moved - only the corner the numbers are read from.
    refreshFields();
}

LayoutPanel::AlignTo LayoutPanel::alignTo() const
{
    if (!m_alignTo) {
        return AlignTo::Timeline;
    }
    return static_cast<AlignTo>(m_alignTo->currentData().toInt());
}

void LayoutPanel::setSelectionBounds(const QRectF& bounds)
{
    setSelectionBounds(QVector<QRectF>{bounds});
}

void LayoutPanel::setSelectionBounds(const QVector<QRectF>& bounds)
{
    m_selectionBounds = bounds;
    m_bounds = QRectF();
    for (const QRectF& item : bounds) m_bounds = m_bounds.isNull() ? item : m_bounds.united(item);
    m_hasSelection = true;
    refreshFields();
    updateEnabled();
}

void LayoutPanel::setFrameRect(const QRectF& frame)
{
    if (!frame.isEmpty()) {
        m_frame = frame;
    }
}

void LayoutPanel::clearSelection()
{
    m_hasSelection = false;
    m_bounds = QRectF();
    m_selectionBounds.clear();
    refreshFields();
    updateEnabled();
}

void LayoutPanel::refreshFields()
{
    if (!m_x) {
        return;
    }
    // Left alone while one of the fields is being typed into: the window pushes
    // the selection's box in again on every rendered frame, and during playback
    // that would overwrite a half-typed number.
    for (const QDoubleSpinBox* field : {m_x, m_y, m_width, m_height}) {
        if (field->hasFocus()) {
            return;
        }
    }
    m_updating = true;
    // X and Y read to the picked point of the box, which is what the direction
    // widget is for: with the centre selected a full-frame 1920x1080 layer
    // reads 960 / 540, exactly as the reference shows it.
    const int index = static_cast<int>(m_direction);
    const double fx = double(index % 3) / 2.0;   // 0, 0.5, 1
    const double fy = double(index / 3) / 2.0;
    m_x->setValue(m_bounds.left() + m_bounds.width() * fx);
    m_y->setValue(m_bounds.top() + m_bounds.height() * fy);
    m_width->setValue(m_bounds.width());
    m_height->setValue(m_bounds.height());
    if (m_scaleLinked && m_scaleLinked->isChecked() && m_bounds.height() > 0.0) {
        m_linkedAspect = m_bounds.width() / m_bounds.height();
    }
    m_updating = false;
}

QRectF LayoutPanel::boundsFromFields() const
{
    const int index = static_cast<int>(m_direction);
    const double fx = double(index % 3) / 2.0;
    const double fy = double(index / 3) / 2.0;
    const double w = qMax(1.0, m_width->value());
    const double h = qMax(1.0, m_height->value());
    return QRectF(m_x->value() - w * fx, m_y->value() - h * fy, w, h);
}

void LayoutPanel::emitBounds(const QRectF& bounds)
{
    if (m_selectionBounds.size() > 1 && !m_bounds.isEmpty()) {
        QVector<QRectF> changed;
        changed.reserve(m_selectionBounds.size());
        const double sx = bounds.width() / m_bounds.width();
        const double sy = bounds.height() / m_bounds.height();
        for (const QRectF& original : m_selectionBounds) {
            changed.append(QRectF(bounds.left() + (original.left() - m_bounds.left()) * sx,
                                  bounds.top() + (original.top() - m_bounds.top()) * sy,
                                  original.width() * sx, original.height() * sy));
        }
        m_selectionBounds = changed;
        m_bounds = bounds;
        refreshFields();
        emit selectionBoundsEdited(changed);
        return;
    }
    m_selectionBounds = {bounds};
    m_bounds = bounds;
    refreshFields();
    emit boundsEdited(bounds);
}

void LayoutPanel::applyAlignment(Qt::Alignment alignment)
{
    if (!m_hasSelection) {
        return;
    }
    const QRectF frame = alignTo() == AlignTo::Timeline ? m_frame : m_bounds;
    if (frame.isEmpty()) {
        return;
    }
    QVector<QRectF> changed = m_selectionBounds;
    for (QRectF& bounds : changed) {
        if (alignment & Qt::AlignLeft) bounds.moveLeft(frame.left());
        else if (alignment & Qt::AlignHCenter) bounds.moveLeft(frame.center().x() - bounds.width() / 2.0);
        else if (alignment & Qt::AlignRight) bounds.moveLeft(frame.right() - bounds.width());
        else if (alignment & Qt::AlignTop) bounds.moveTop(frame.top());
        else if (alignment & Qt::AlignVCenter) bounds.moveTop(frame.center().y() - bounds.height() / 2.0);
        else if (alignment & Qt::AlignBottom) bounds.moveTop(frame.bottom() - bounds.height());
    }
    if (changed.size() == 1) emitBounds(changed.first());
    else {
        m_selectionBounds = changed;
        m_bounds = QRectF();
        for (const QRectF& item : changed) m_bounds = m_bounds.isNull() ? item : m_bounds.united(item);
        refreshFields();
        emit selectionBoundsEdited(changed);
    }
}

void LayoutPanel::applyDistribution(const QString& mode)
{
    if (m_selectionBounds.size() < 3) return;
    const bool horizontal = mode.contains(QLatin1String("left"))
                            || mode.contains(QLatin1String("horizontally"))
                            || mode.contains(QLatin1String("right"));
    const bool useEnd = mode.endsWith(QLatin1String("right"))
                        || mode.endsWith(QLatin1String("bottom"));
    const bool useCenter = mode.contains(QLatin1String("horizontally"))
                           || mode.contains(QLatin1String("vertically"));
    QVector<int> order(m_selectionBounds.size());
    std::iota(order.begin(), order.end(), 0);
    const auto anchor = [&](const QRectF& rect) {
        if (horizontal) return useCenter ? rect.center().x() : (useEnd ? rect.right() : rect.left());
        return useCenter ? rect.center().y() : (useEnd ? rect.bottom() : rect.top());
    };
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return anchor(m_selectionBounds.at(a)) < anchor(m_selectionBounds.at(b));
    });
    QVector<QRectF> changed = m_selectionBounds;
    const double first = anchor(changed.at(order.first()));
    const double step = (anchor(changed.at(order.last())) - first) / (order.size() - 1);
    for (int rank = 1; rank + 1 < order.size(); ++rank) {
        QRectF& rect = changed[order.at(rank)];
        const double target = first + step * rank;
        if (horizontal) rect.translate(target - anchor(rect), 0.0);
        else rect.translate(0.0, target - anchor(rect));
    }
    m_selectionBounds = changed;
    m_bounds = QRectF();
    for (const QRectF& item : changed) m_bounds = m_bounds.isNull() ? item : m_bounds.united(item);
    refreshFields();
    emit selectionBoundsEdited(changed);
}

void LayoutPanel::updateEnabled()
{
    if (!m_x) {
        return;
    }
    for (QWidget* field : {static_cast<QWidget*>(m_x), static_cast<QWidget*>(m_y),
                           static_cast<QWidget*>(m_width), static_cast<QWidget*>(m_height),
                           static_cast<QWidget*>(m_scaleLinked)}) {
        field->setEnabled(m_hasSelection);
    }

    // Aligning to the Selection means aligning the selected objects to each
    // other, and distributing means spreading three or more of them evenly.
    // This port selects one layer at a time, so both are switched off rather
    // than left as buttons that do nothing when pressed.
    const bool toTimeline = alignTo() == AlignTo::Timeline;
    if (m_alignRow) {
        const bool canAlign = m_hasSelection && (toTimeline || m_selectionBounds.size() > 1);
        m_alignRow->setEnabled(canAlign);
        m_alignRow->setToolTip(canAlign ? QString() : tr("Select at least two objects."));
    }
    if (m_distributeRow) {
        const bool canDistribute = m_selectionBounds.size() >= 3;
        m_distributeRow->setEnabled(canDistribute);
        m_distributeRow->setToolTip(canDistribute ? QString()
                                                   : tr("Select at least three objects."));
    }
}

} // namespace ui
} // namespace openvegas
