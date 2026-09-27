#pragma once
#include <QWidget>
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <functional>
namespace openvegas::ui {
// Capture before showing the overlay, so the picker never samples itself.
class ScreenColorPicker : public QWidget
{
public:
    ScreenColorPicker(QWidget* parent, std::function<void(QColor)> picked)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_picked(std::move(picked))
    {
        setObjectName(QStringLiteral("screenColorPicker"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        setCursor(Qt::CrossCursor);
        QRect bounds;
        for (QScreen* screen : QGuiApplication::screens()) {
            const QPixmap pixels = screen->grabWindow(0);
            m_screens.append({screen->geometry(), pixels.toImage()});
            bounds = bounds.united(screen->geometry());
        }
        setGeometry(bounds);
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        for (const auto& screen : m_screens)
            p.drawImage(screen.geometry.translated(-geometry().topLeft()), screen.pixels);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const QPoint global = event->globalPosition().toPoint();
            for (const auto& screen : m_screens) {
                if (!screen.geometry.contains(global) || screen.pixels.isNull()) continue;
                const QPoint local = global - screen.geometry.topLeft();
                const int x = local.x() * screen.pixels.width() / screen.geometry.width();
                const int y = local.y() * screen.pixels.height() / screen.geometry.height();
                m_picked(screen.pixels.pixelColor(x, y));
                break;
            }
        }
        close();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) close();
    }
private:
    struct Screen { QRect geometry; QImage pixels; };
    QList<Screen> m_screens;
    std::function<void(QColor)> m_picked;
};

}
