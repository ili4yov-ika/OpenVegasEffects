#pragma once

#include <QString>

#include "license/LicenseTypes.h"

namespace openvegas {
namespace license {

class LicenseManager
{
public:
    virtual ~LicenseManager() = default;

    virtual LicenseStatus status() const = 0;
    virtual LicenseType type() const = 0;
    virtual HostEdition edition() const = 0;
    virtual LicenseInfo info() const = 0;

    virtual bool isActivated() const = 0;
    virtual bool isFeatureEnabled(const QString& featureId) const = 0;

    virtual QString userName() const = 0;
};

} // namespace license
} // namespace openvegas