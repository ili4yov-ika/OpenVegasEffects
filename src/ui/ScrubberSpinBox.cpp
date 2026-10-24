#include "ui/ScrubberSpinBox.h"

#include <QApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QWheelEvent>

#include <cmath>

// The reference's scrubber number field: read-only text that changes its
// value while dragged left or right (one step per pixel; with Shift, in
// snapping increments every 5 pixels; with Ctrl, where enabled, a click
// flips the sign), and turns into an ordinary editor when clicked or
// tabbed into. Escape restores the value the edit started with. While
// dragging, the pointer is hidden and held in place.

namespace openvegas::ui {

DoubleScrubberSpinBox::DoubleScrubberSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    // Numbers as the reference writes them (QString::number): a point, no
    // grouping, whatever the language.
    setLocale(QLocale::c());
    lineEdit()->installEventFilter(this);
    // Leaving the text, or Return, scrubs again.
    connect(this, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!isReadOnly()) setScrubberMode();
        focusParentIfNoFocus();
    });
    setDecimals(3);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(this, &QDoubleSpinBox::valueChanged, this, [this] { updateGeometry(); });
    setScrubberMode();
    setKeyboardTracking(false);
    setAccelerated(true);
    setAttribute(Qt::WA_InputMethodEnabled, false);
}

DoubleScrubberSpinBox::~DoubleScrubberSpinBox()
{
    showPointer();
}

// The next multiple of increment above (upwards) or below the value.
double DoubleScrubberSpinBox::snapped(double value, double increment, bool upwards)
{
    if (increment == 0.0) return value;
    int nudge = upwards ? 1 : 0;
    if (value < 0.0) nudge = upwards ? 0 : -1;
    double steps = value / increment;
    const double whole = double(int(steps));
    // A multiple already (to 1e-12 of a step) stays where it is.
    if (std::min(std::abs(steps), std::abs(whole)) < std::abs(steps - whole) * 1e12)
        steps = double(int(nudge + steps));
    return steps * increment;
}

// The value with at most `decimals` decimals and no trailing zeros beyond
// the first `keep`.
QString DoubleScrubberSpinBox::formatted(double value, int decimals, int keep)
{
    const QString text = QString::number(value, 'f', qMax(0, decimals));
    const int dot = int(text.indexOf(QLatin1Char('.')));
    if (dot < 0) return text;
    static const QRegularExpression nonZero(QStringLiteral("[1-9]"));
    const int lastNonZero = int(text.lastIndexOf(nonZero));
    if (lastNonZero < dot) {
        // Nothing after the point: `keep` zeros, or none.
        return keep > 0 ? QString::number(value, 'f', keep) : text.left(dot);
    }
    return text.left(qMax(lastNonZero + 1, keep > 0 ? dot + 1 + keep : 0));
}

// Read-only, scrubbed by dragging, with the prefix and suffix shown.
void DoubleScrubberSpinBox::setScrubberMode()
{
    const bool blocked = blockSignals(true);
    setContextMenuPolicy(Qt::NoContextMenu);
    lineEdit()->deselect();
    setReadOnly(true);
    lineEdit()->setCursor(Qt::SizeHorCursor);
    setPrefix(m_prefix);
    setSuffix(m_suffix);
    blockSignals(blocked);
}

// Edited as text: the bare number, all selected.
void DoubleScrubberSpinBox::setEditMode()
{
    m_prefix = prefix();
    m_suffix = suffix();
    setReadOnly(false);
    setPrefix(QString());
    setSuffix(QString());
    setContextMenuPolicy(Qt::DefaultContextMenu);
    lineEdit()->selectAll();
}

// Back to the value before the drag or edit, without reporting a change.
void DoubleScrubberSpinBox::revert()
{
    const bool blocked = blockSignals(true);
    setValue(m_startValue);
    if (!isReadOnly()) {
        setScrubberMode();
        focusParentIfNoFocus();
    }
    blockSignals(blocked);
}

// A field that takes no focus hands it back to its parent after an edit.
void DoubleScrubberSpinBox::focusParentIfNoFocus()
{
    if (hasFocus() && focusPolicy() == Qt::NoFocus && parentWidget() && !parentWidget()->hasFocus())
        parentWidget()->setFocus(Qt::OtherFocusReason);
}

void DoubleScrubberSpinBox::showPointer()
{
    if (!m_pointerHidden) return;
    QApplication::restoreOverrideCursor();
    m_pointerHidden = false;
}

// The mouse on the text: press starts a scrub, moving changes the value,
// release ends it - or, without a drag, starts editing the text.
bool DoubleScrubberSpinBox::eventFilter(QObject* object, QEvent* event)
{
    if (object != lineEdit() || !isEnabled()) return QDoubleSpinBox::eventFilter(object, event);
    const bool flipping = m_flipEnabled && m_pressModifiers == Qt::ControlModifier;
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        m_pressModifiers = mouse->modifiers();
        if (!isReadOnly() || mouse->button() != Qt::LeftButton) break;
        m_dragged = false;
        m_startValue = value();
        m_pressPosition = mouse->globalPosition().toPoint();
        m_accumulated = 0;
        if (!m_pointerHidden) {
            QApplication::setOverrideCursor(Qt::BlankCursor);
            m_pointerHidden = true;
        }
        if (m_flipEnabled && m_pressModifiers == Qt::ControlModifier) setValue(-value());
        else m_snapping = m_pressModifiers == Qt::ShiftModifier;
        event->accept();
        emit editingStarted();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        showPointer();
        if (mouse->button() != Qt::LeftButton) {
            event->ignore();
            return true;
        }
        if (m_doubleClicked) {
            m_doubleClicked = false;
            event->ignore();
            return true;
        }
        event->accept();
        if (m_pressModifiers != Qt::ControlModifier) {
            m_snapping = false;
            if (m_dragged) {
                emit mouseReleased();
            } else if (isReadOnly()) {
                // A click: the text is edited.
                if (hasFocus()) setEditMode();
                else setFocus(Qt::OtherFocusReason);
            }
            m_dragged = false;
        }
        m_pressModifiers = Qt::NoModifier;
        return true;
    }
    case QEvent::MouseButtonDblClick:
        m_doubleClicked = true;
        if (flipping || isReadOnly()) {
            event->accept();
            return true;
        }
        event->ignore();
        break;
    case QEvent::MouseMove: {
        if (flipping) {
            event->accept();
            return true;
        }
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!isReadOnly() || !(mouse->buttons() & Qt::LeftButton)) break;
        const QPoint delta = mouse->globalPosition().toPoint() - m_pressPosition;
        if (!m_dragged && delta.manhattanLength() < QApplication::startDragDistance()) return true;
        m_dragged = true;
        // Held at either end of the range.
        if ((value() != minimum() || delta.x() >= 0) && (value() != maximum() || delta.x() < 1))
            m_accumulated += delta.x();
        const double next = m_snapping
            ? snapped(m_startValue + double(m_accumulated / 5) * m_snappingIncrement, m_snappingIncrement,
                      m_accumulated > 0)
            : m_startValue + singleStep() * double(m_accumulated);
        setValue(next);
        // The pointer stays where the drag began, so the drag never runs out
        // of screen.
        QCursor::setPos(m_pressPosition);
        event->accept();
        return true;
    }
    default:
        break;
    }
    return QDoubleSpinBox::eventFilter(object, event);
}

// Tabbing in edits the text; a mouse focus does not (the click decides).
void DoubleScrubberSpinBox::focusInEvent(QFocusEvent* event)
{
    const Qt::FocusReason reason = event->reason();
    if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::OtherFocusReason) {
        m_startValue = value();
        const bool blocked = blockSignals(true);
        setValue(m_startValue);
        setEditMode();
        QDoubleSpinBox::focusInEvent(event);
        blockSignals(blocked);
    } else if (isReadOnly()) {
        event->accept();
    }
}

// Leaving keeps a typed value; leaving it unchanged just goes back to
// scrubbing, and losing focus otherwise reverts.
void DoubleScrubberSpinBox::focusOutEvent(QFocusEvent* event)
{
    switch (event->reason()) {
    case Qt::MouseFocusReason:
    case Qt::TabFocusReason:
    case Qt::BacktabFocusReason:
    case Qt::ActiveWindowFocusReason:
    case Qt::OtherFocusReason:
        // Left unchanged: back to scrubbing without an edit.
        if (valueFromText(text()) == m_startValue) {
            const bool blocked = blockSignals(true);
            setValue(m_startValue);
            if (!isReadOnly()) {
                setScrubberMode();
                focusParentIfNoFocus();
            }
            blockSignals(blocked);
            return;
        }
        break;
    case Qt::PopupFocusReason:
        return;
    default:
        revert();
        break;
    }
    QDoubleSpinBox::focusOutEvent(event);
}

void DoubleScrubberSpinBox::keyPressEvent(QKeyEvent* event)
{
    QDoubleSpinBox::keyPressEvent(event);
    if (event->key() == Qt::Key_Escape) {
        revert();
        event->accept();
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) event->accept();
}

// A field that takes no focus lets the wheel scroll its parent.
void DoubleScrubberSpinBox::wheelEvent(QWheelEvent* event)
{
    if (focusPolicy() == Qt::NoFocus && isReadOnly()) {
        QWidget::wheelEvent(event);
        return;
    }
    QDoubleSpinBox::wheelEvent(event);
}

void DoubleScrubberSpinBox::stepBy(int steps)
{
    if (QGuiApplication::keyboardModifiers() != Qt::ShiftModifier
        && QGuiApplication::queryKeyboardModifiers() != Qt::ShiftModifier) {
        QDoubleSpinBox::stepBy(steps);
        return;
    }
    // Shift: to the next multiple of the snapping increment.
    const double from = valueFromText(text()) + double(steps) * std::pow(10.0, -4.0);
    setValue(snapped(from, m_snappingIncrement, steps > 0));
    selectAll();
}

// Scrubbing shows the display decimals; editing up to three.
QString DoubleScrubberSpinBox::textFromValue(double value) const
{
    return isReadOnly() ? formatted(value, int(m_displayDecimals), int(m_displayDecimals))
                        : formatted(value, 3, 0);
}

// As wide as its text, so the field grows and shrinks with the value.
QSize DoubleScrubberSpinBox::sizeHint() const
{
    QSize hint = QDoubleSpinBox::sizeHint();
    hint.setWidth(fontMetrics().horizontalAdvance(text()) + 10);
    return hint;
}

QSize DoubleScrubberSpinBox::minimumSizeHint() const
{
    QSize hint = QDoubleSpinBox::minimumSizeHint();
    hint.setWidth(20);
    return hint;
}

} // namespace openvegas::ui
