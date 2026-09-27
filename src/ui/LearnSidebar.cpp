#include "ui/LearnSidebar.h"

#ifdef OPENVEGAS_HAVE_WEBENGINE

#include <QUrl>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>
#include <QWidget>

namespace openvegas {
namespace ui {

LearnBridge::LearnBridge(QObject* parent)
    : QObject(parent)
{
}

void LearnBridge::openTutorials()
{
    emit tutorialsRequested();
}

void LearnBridge::showCommand(const QString& name)
{
    emit commandRequested(name);
}

LearnSidebar::LearnSidebar(QWidget* parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Learn"));
    setObjectName(QStringLiteral("LearnSidebar"));

    QWidget* root = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new QWebEngineView(root);
    m_view->setObjectName(QStringLiteral("learnWebViewSpace"));
    layout->addWidget(m_view);
    setWidget(root);

    // JS <-> C++ bridge, as in the reference (Qt5WebChannel.dll import).
    m_bridge = new LearnBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("host"), m_bridge);
    m_view->page()->setWebChannel(m_channel);

    connect(m_bridge, &LearnBridge::tutorialsRequested,
            this, &LearnSidebar::tutorialsRequested);
    connect(m_bridge, &LearnBridge::commandRequested,
            this, &LearnSidebar::commandRequested);

    loadHome();
}

void LearnSidebar::shutdown()
{
    if (!m_view) {
        return;
    }
    m_view->page()->setWebChannel(nullptr);
    delete m_view;
    m_view = nullptr;
}

void LearnSidebar::loadHome()
{
    load(QUrl(QStringLiteral("qrc:/learn/index.html")));
}

void LearnSidebar::load(const QUrl& url)
{
    if (m_view) {
        m_view->load(url);
    }
}

} // namespace ui
} // namespace openvegas

#endif // OPENVEGAS_HAVE_WEBENGINE
