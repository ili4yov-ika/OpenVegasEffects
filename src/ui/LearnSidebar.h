#pragma once

#ifdef OPENVEGAS_HAVE_WEBENGINE

#include <QDockWidget>
#include <QObject>
#include <QString>

class QUrl;
class QWebChannel;
class QWebEngineView;

namespace openvegas {
namespace ui {

// Bridge object exposed to the page over Qt WebChannel under the name "host".
// The reference wires its HTML surfaces to C++ the same way (the exe imports
// Qt5WebChannel.dll alongside Qt5WebEngineWidgets.dll) and signals the host
// side with events such as "LearnCheckBoxToggled" (0x1414e627e).
class LearnBridge : public QObject
{
    Q_OBJECT

public:
    explicit LearnBridge(QObject* parent = nullptr);

public slots:
    void openTutorials();
    void showCommand(const QString& name);

signals:
    void tutorialsRequested();
    void commandRequested(const QString& name);
};

// Mirrors the reference "Learn Sidebar": a QWebEngineView docked on the right,
// toggled by "Toggle Learn Sidebar" (0x1412d0908) and remembered across runs
// in the setting learnSidebarIsOpen (0x1412c10d0). The view carries the
// reference object name learnWebViewSpace (0x1412c0e68).
//
// Only built when Qt WebEngine is available; on Windows that means an MSVC
// kit, since Qt does not ship WebEngine for MinGW.
class LearnSidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit LearnSidebar(QWidget* parent = nullptr);

    // Loads the built-in page (qrc:/learn/index.html) or, when `url` is given,
    // an external lesson page.
    void loadHome();
    void load(const QUrl& url);

    // Destroys the web view (and with it its page) before Qt releases the
    // default QWebEngineProfile. Without this the profile outlives the page
    // and Qt logs "Release of profile requested but WebEnginePage still not
    // deleted. Expect troubles !" during shutdown.
    void shutdown();

signals:
    void tutorialsRequested();
    void commandRequested(const QString& name);

private:
    QWebEngineView* m_view = nullptr;
    QWebChannel* m_channel = nullptr;
    LearnBridge* m_bridge = nullptr;
};

} // namespace ui
} // namespace openvegas

#endif // OPENVEGAS_HAVE_WEBENGINE
