#include "license/OpenLicenseManager.h"

namespace openvegas {
namespace license {

QString toString(HostEdition edition)
{
    switch (edition) {
    case HostEdition::Vegas:  return QStringLiteral("Vegas");
    case HostEdition::HitFilm: return QStringLiteral("HitFilm");
    case HostEdition::Free:   return QStringLiteral("Free");
    }
    return QStringLiteral("Unknown");
}

QString toString(LicenseStatus status)
{
    switch (status) {
    case LicenseStatus::Unknown:           return QStringLiteral("Unknown");
    case LicenseStatus::Valid:             return QStringLiteral("Valid");
    case LicenseStatus::Expired:           return QStringLiteral("Expired");
    case LicenseStatus::NotFound:          return QStringLiteral("NotFound");
    case LicenseStatus::ActivationRequired: return QStringLiteral("ActivationRequired");
    }
    return QStringLiteral("Unknown");
}

OpenLicenseManager::OpenLicenseManager()
{
    m_info.status = LicenseStatus::Valid;
    m_info.type = LicenseType::Free;
    m_info.edition = HostEdition::Vegas;
    m_info.productName = QStringLiteral("OpenVegas Effects");
    m_info.userName = QStringLiteral("Local User");
}

LicenseStatus OpenLicenseManager::status() const
{
    return m_info.status;
}

LicenseType OpenLicenseManager::type() const
{
    return m_info.type;
}

HostEdition OpenLicenseManager::edition() const
{
    return m_info.edition;
}

LicenseInfo OpenLicenseManager::info() const
{
    return m_info;
}

bool OpenLicenseManager::isActivated() const
{
    return m_info.status == LicenseStatus::Valid;
}

bool OpenLicenseManager::isFeatureEnabled(const QString& featureId) const
{
    Q_UNUSED(featureId);
    return true;
}

QString OpenLicenseManager::userName() const
{
    return m_info.userName;
}

} // namespace license
} // namespace openvegas