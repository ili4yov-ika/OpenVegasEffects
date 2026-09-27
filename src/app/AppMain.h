#pragma once

#include <QPixmap>
#include <QSharedPointer>
#include <memory>

#include "app/Settings.h"
#include "app/UserDataPaths.h"
#include "cache/CacheDB.h"
#include "composition/Composition.h"
#include "license/LicenseManager.h"
#include "license/OpenLicenseManager.h"
#include "media/MediaManager.h"
#include "plugin/PluginManager.h"
#include "render/RenderManager.h"
#include "ui/MainWindow.h"

namespace openvegas {
namespace app {

class AppMain : public QObject
{
    Q_OBJECT

public:
    explicit AppMain(QObject* parent = nullptr);
    ~AppMain() override;

    int run(QApplication* application, const QString& projectToOpen = QString());

    Settings* settings() const { return m_settings.get(); }
    license::LicenseManager* licenseManager() const { return m_licenseManager.get(); }
    plugin::PluginManager* pluginManager() const { return m_pluginManager.get(); }
    media::MediaManager* mediaManager() const { return m_mediaManager.get(); }
    composition::Composition* composition() const { return m_composition.get(); }
    cache::CacheDB* cacheDB() const { return m_cacheDB.get(); }
    render::RenderManager* renderManager() const { return m_renderManager.get(); }

private:
    void initializeFolders();
    void initializeSettings();
    void initializeCache();
    void initializeLicense();
    void initializePlugins();
    void initializeProject();
    void initializeUi();

    void logPluginStatus() const;

    std::unique_ptr<Settings> m_settings;
    UserDataPaths m_paths;
    std::unique_ptr<license::LicenseManager> m_licenseManager;
    std::shared_ptr<plugin::PluginManager> m_pluginManager;
    std::shared_ptr<composition::Composition> m_composition;
    std::shared_ptr<media::MediaManager> m_mediaManager;
    std::unique_ptr<cache::CacheDB> m_cacheDB;
    std::unique_ptr<render::RenderManager> m_renderManager;
    ui::MainWindow* m_mainWindow = nullptr;
};

} // namespace app
} // namespace openvegas