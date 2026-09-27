#pragma once

#include "license/LicenseManager.h"

namespace openvegas {
namespace license {

class OpenLicenseManager : public LicenseManager
{
public:
    OpenLicenseManager();

    LicenseStatus status() const override;
    LicenseType type() const override;
    HostEdition edition() const override;
    LicenseInfo info() const override;

    bool isActivated() const override;
    bool isFeatureEnabled(const QString& featureId) const override;

    QString userName() const override;

private:
    LicenseInfo m_info;
};

} // namespace license
} // namespace openvegas