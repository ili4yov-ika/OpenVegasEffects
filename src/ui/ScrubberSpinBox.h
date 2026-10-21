#pragma once

#include <QDoubleSpinBox>
#include <QPoint>
#include <QString>

namespace openvegas::ui {

// biff::ui::widgets::DoubleScrubberSpinBox (Widgets.dll), the reference's
// numeric field - the Layout panel's X, Y, Width and Height among others.
//
// Until it is edited it is a scrubber: read-only, a horizontal-resize cursor
// over the text, the value shown with DisplayDecimals (1) and trailing zeros
// trimmed. Dragging it with the left button changes the value by one
// singleStep a pixel, from the value at the press, with the pointer hidden
// and held where it went down (CursorUtilities::SetPosition), and stops at
// the minimum or maximum; with Shift held at the press it moves in snapping
// increments (10) every five pixels, landing on a multiple in the direction of
// the drag; with SetFlipEnabled, Ctrl at the press negates the value. A click
// that does not drag edits the text (focused already: SetEditMode; otherwise
// focus, which enters it); Tab, Backtab or other focus also enter it, with
// prefix and suffix dropped, the text selected and three decimals. Escape,
// or leaving it unchanged, goes back to the value it had (Revert) and to
// scrubbing; editingFinished does too. Shift with the arrow keys snaps the
// same way as dragging. The wheel only edits when the field takes focus.
class DoubleScrubberSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    static constexpr double kDefaultSnappingIncrement = 10.0;

    explicit DoubleScrubberSpinBox(QWidget* parent = nullptr);
    ~DoubleScrubberSpinBox() override;

    unsigned displayDecimals() const { return m_displayDecimals; }
    void setDisplayDecimals(unsigned decimals) { m_displayDecimals = decimals; update(); }
    double snappingIncrement() const { return m_snappingIncrement; }
    void setSnappingIncrement(double increment) { m_snappingIncrement = increment; }
    bool flipEnabled() const { return m_flipEnabled; }
    void setFlipEnabled(bool enabled) { m_flipEnabled = enabled; }

    void setScrubberMode();
    void setEditMode();
    void revert();
    bool isScrubbing() const { return m_dragged; }

    // `value` moved to a multiple of `increment`, up or down as `upwards`
    // says, unless it is one already (the reference's helper 0x180028820).
    static double snapped(double value, double increment, bool upwards);
    // The text the field shows for `value`: `decimals` places, trailing zeros
    // trimmed, `keep` places kept when there are none (0x1800288c0).
    static QString formatted(double value, int decimals, int keep);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // A drag or a flip began (signal 0, EditingStarted).
    void editingStarted();
    // The button went up after a drag (signal 1, MouseReleased).
    void mouseReleased();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void stepBy(int steps) override;
    QString textFromValue(double value) const override;

private:
    void showPointer();
    void focusParentIfNoFocus();

    int m_accumulated = 0;          // pixels dragged, +0x00
    double m_startValue = 0.0;      // +0x08
    double m_snappingIncrement = kDefaultSnappingIncrement;  // +0x10
    bool m_snapping = false;        // +0x18
    bool m_dragged = false;         // +0x1c
    bool m_flipEnabled = false;     // +0x1d
    bool m_doubleClicked = false;   // +0x1e
    Qt::KeyboardModifiers m_pressModifiers;  // +0x20
    unsigned m_displayDecimals = 1; // +0x24
    QString m_prefix;               // +0x28
    QString m_suffix;               // +0x30
    QPoint m_pressPosition;         // +0x38
    bool m_pointerHidden = false;
};

} // namespace openvegas::ui
