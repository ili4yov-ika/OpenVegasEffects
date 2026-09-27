#pragma once
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <cmath>

namespace openvegas::ui {
// The reference uses Integer/DoubleScrubberSpinBox. Normal typing and arrow
// buttons are retained; dragging the value adjusts it without opening a dialog.
template<class Base> class TextScrubber : public Base
{
public:
    explicit TextScrubber(QWidget* parent = nullptr) : Base(parent)
    {
        this->lineEdit()->installEventFilter(this);
        this->setKeyboardTracking(false);
    }
    ~TextScrubber() override { endDrag(); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == this->lineEdit()) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() == Qt::LeftButton) {
                    m_pressed = true;
                    m_start = mouse->globalPosition();
                    m_value = this->value();
                }
            } else if (event->type() == QEvent::MouseMove && m_pressed) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                const double delta = mouse->globalPosition().x() - m_start.x();
                if (!m_dragging && std::abs(delta) >= QApplication::startDragDistance()) {
                    m_dragging = true;
                    QApplication::setOverrideCursor(Qt::SizeHorCursor);
                }
                if (m_dragging) {
                    const double speed = mouse->modifiers().testFlag(Qt::ShiftModifier) ? 0.1
                                       : mouse->modifiers().testFlag(Qt::ControlModifier) ? 10.0 : 1.0;
                    const double value = m_value + std::round(delta / 2.0) * this->singleStep() * speed;
                    this->setValue(value);
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                const bool dragged = m_dragging;
                endDrag();
                if (dragged) { emit this->editingFinished(); return true; }
            } else if (event->type() == QEvent::KeyPress
                       && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && m_dragging) {
                this->setValue(m_value);
                endDrag();
                emit this->editingFinished();
                return true;
            } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
                endDrag();
            }
        }
        return Base::eventFilter(object, event);
    }
    void wheelEvent(QWheelEvent* event) override
    {
        // Scrolling the panel must not accidentally edit a value under the mouse.
        if (this->hasFocus()) Base::wheelEvent(event);
        else event->ignore();
    }
private:
    void endDrag()
    {
        if (m_dragging) QApplication::restoreOverrideCursor();
        m_dragging = false;
        m_pressed = false;
    }
    bool m_pressed = false;
    bool m_dragging = false;
    QPointF m_start;
    double m_value = 0;
};
}
