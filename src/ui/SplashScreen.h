#pragma once

#include <QPixmap>
#include <QSplashScreen>
#include <QString>

class QMouseEvent;
class QPainter;

namespace openvegas {
namespace ui {

// Lightweight splash shown while the application starts up.
//
// Reference behaviour (FUN_1401c2ce0 / biff::ui::widgets::SplashScreen): a
// frameless splash is painted from a bundled image and shown after the theme
// is applied, centred on the screen that holds the pointer (falling back to
// the primary screen), scaled with that screen's device pixel ratio. It is
// completed - hidden - right before the event loop starts, so the main window
// doesn't pop in over a dead image.
class SplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    // QSplashScreen needs a pixmap at construct time even though the real
    // image is only loaded later, hence the empty default.
    explicit SplashScreen(QWidget* parent = nullptr)
        : QSplashScreen(QPixmap())
    {
        Q_UNUSED(parent);
        setWindowFlag(Qt::FramelessWindowHint, true);
    }

    // Paints the splash from the ":/icons/splash.svg" resource (our own art,
    // not a copy of the reference's splash resource) and shows it centred on
    // the screen under the mouse cursor, honouring that screen's DPI scale.
    void showSplash();

    // Shows the current startup stage in the free area at the lower right.
    // The reference application does not call QSplashScreen::showMessage: its
    // product name is baked into :/images/images/splash.png. Keeping the
    // product title in our SVG and drawing only the changing stage here avoids
    // blurring the vector artwork.
    void setStatus(const QString& status);

    // Hides the splash once startup is done. Mirrors SplashScreen::Complete()
    // being invoked just before the main window's event loop.
    void complete(QWidget* mainWindow = nullptr);

protected:
    // QSplashScreen normally disappears when clicked.  Startup is not complete
    // at that point, so the reference consumes the click in its event filter.
    void mousePressEvent(QMouseEvent* event) override;
    void drawContents(QPainter* painter) override;

private:
    QString m_status;
};

} // namespace ui
} // namespace openvegas
