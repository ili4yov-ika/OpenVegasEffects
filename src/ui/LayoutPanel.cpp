#include "ui/LayoutPanel.h"
#include "ui_Layout.h"
#include "ui/Theme.h"

#include <QApplication>

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <numeric>

namespace openvegas {
namespace ui {

namespace {

// Icons are copied unchanged from the original RCC resource tree. Include
// each native state and its @2x variant so disabled and HiDPI buttons keep
// the same artwork rather than Qt generating grey, blurred approximations.
QIcon makeGlyph(const QString& name)
{
    QIcon icon;
    const auto addState = [&](const QString& suffix, QIcon::Mode mode, QIcon::State state) {
        const QString base = QStringLiteral(":/icons/layout/") + name + suffix;
        icon.addFile(base + QStringLiteral(".png"), QSize(), mode, state);
        icon.addFile(base + QStringLiteral("@2x.png"), QSize(), mode, state);
    };
    addState(QString(), QIcon::Normal, QIcon::Off);
    addState(QStringLiteral("-hover"), QIcon::Active, QIcon::Off);
    addState(QStringLiteral("-checked"), QIcon::Normal, QIcon::On);
    addState(QStringLiteral("-checked"), QIcon::Active, QIcon::On);
    addState(QStringLiteral("-disabled"), QIcon::Disabled, QIcon::Off);
    addState(QStringLiteral("-disabled"), QIcon::Disabled, QIcon::On);
    return icon;
}

} // namespace

LayoutPanel::LayoutPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::LayoutPanel form;
    form.setupUi(this);
    m_transformWidget = form.TransformWidget;
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
    setGlyph(form.toolButtonCounterClockWise, QStringLiteral("anticlockwise90"));
    setGlyph(form.toolButtonClockWise, QStringLiteral("clockwise90"));
    setGlyph(m_scaleLinked, QStringLiteral("linked"));
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
    const bool dark = !qApp->styleSheet().isEmpty();
    const auto& colors = themeColors();
    const QString border = dark ? colors.lineColor.name() : palette().mid().color().name();
    const QString heading = dark ? colors.fillPanel1.name() : palette().alternateBase().color().name();
    const QString hover = dark ? colors.buttonHover.name() : palette().button().color().name();
    const QString accent = dark ? colors.focus.name() : palette().highlight().color().name();
    form.layoutContentWidget->setStyleSheet(QStringLiteral(
        "QToolButton[layoutIcon=\"true\"] { border:0; border-radius:0; padding:4px;"
        "min-width:16px; min-height:16px; background:transparent; }"
        "QToolButton[layoutIcon=\"true\"]:hover { background:%1; }"
        "QToolButton[layoutIcon=\"true\"]:checked { background:%2; }"
        "QToolButton[layoutIcon=\"true\"]:disabled { background:transparent; }"
        "QGroupBox { border:1px solid %3; border-radius:0; margin-top:0; }"
        "QLabel#layoutAlignmentHeading { background:%4; padding:6px 8px; }")
            .arg(hover, accent, border, heading));
    const char* directionIcons[] = {
        "top-left", "top", "top-right", "left", "center", "right",
        "bottom-left", "bottom", "bottom-right",
    };
    for (int i = 0; i < 9; ++i) {
        setGlyph(directionButtons[i], QString::fromLatin1(directionIcons[i]));
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
    refreshFields(true);
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
    if (bounds.isEmpty()) {
        clearSelection();
        return;
    }
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
    refreshFields(true);
    updateEnabled();
}

void LayoutPanel::refreshFields(bool force)
{
    if (!m_x) {
        return;
    }
    // Left alone while one of the fields is being typed into: the window pushes
    // the selection's box in again on every rendered frame, and during playback
    // that would overwrite a half-typed number.
    for (const QDoubleSpinBox* field : {m_x, m_y, m_width, m_height}) {
        if (!force && field->hasFocus()) {
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

    if (m_transformWidget) m_transformWidget->setEnabled(m_hasSelection);
    if (m_alignTo) m_alignTo->setEnabled(m_hasSelection);

    // Alignment to the selection requires two objects; distribution requires
    // at least three. Orientation and dimensions also need a selection.
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
