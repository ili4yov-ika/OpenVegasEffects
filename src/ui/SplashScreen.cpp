#include "ui/SplashScreen.h"

#include <QCursor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace openvegas {
namespace ui {

void SplashScreen::showSplash()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    // The reference constructor receives :/images/images/splash.png; this port
    // keeps the replacement artwork as a resolution-independent SVG resource.
    QPixmap pixmap(QStringLiteral(":/icons/splash.svg"));
    if (pixmap.isNull()) {
        return;
    }
    setPixmap(pixmap);

    // Centre on the screen's available geometry, as the reference positions
    // the splash from QScreen::availableGeometry.
    const QRect geo = screen->availableGeometry();
    move(geo.center().x() - width() / 2, geo.center().y() - height() / 2);

    show();
    setStatus(tr("Starting..."));
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::setStatus(const QString& status)
{
    m_status = status;
    if (!isVisible()) {
        return;
    }
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::complete(QWidget* mainWindow)
{
    finish(mainWindow);
}

void SplashScreen::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void SplashScreen::drawContents(QPainter* painter)
{
    if (!painter || m_status.isEmpty()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    QFont statusFont = font();
    statusFont.setPixelSize(12);
    statusFont.setWeight(QFont::Medium);
    painter->setFont(statusFont);
    painter->setPen(QColor(255, 255, 255, 220));

    const QRect statusRect(330, height() - 54, width() - 358, 28);
    const QString text = painter->fontMetrics().elidedText(
        m_status, Qt::ElideRight, statusRect.width());
    painter->drawText(statusRect, Qt::AlignRight | Qt::AlignVCenter, text);
    painter->restore();
}

} // namespace ui
} // namespace openvegas
