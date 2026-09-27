#pragma once

#include <QString>

namespace openvegas {
namespace license {

enum class HostEdition
{
    // Values recovered from the reference: BiffHostEdition is a packed enum
    // whose two observed products are 0x9c4 (2500, "Vegas Effects") and
    // 0x898 (2200, "HitFilm"). These numeric values flow straight into
    // PluginManager::Create (they are not sequential 1/2/3 ids).
    Vegas  = 0x9c4, // 2500
    HitFilm = 0x898, // 2200
    Free   = 0,
};

// Product edition counter, recovered from the reference bootstrap. The
// product edition (5000 / 6000) is what the reference persists and uses for
// version strings and activation labels; it is mapped onto BiffHostEdition
// before PluginManager::Create:
//   product 5000 -> HostEdition::Vegas (0x9c4)
//   product 6000 -> HostEdition::HitFilm (0x898)
enum class ProductEdition
{
    Vegas  = 5000,
    HitFilm = 6000,
};

// Maps a product edition onto the BiffHostEdition the plugin manager expects.
inline HostEdition toHostEdition(ProductEdition product)
{
    switch (product) {
    case ProductEdition::Vegas:
        return HostEdition::Vegas;
    case ProductEdition::HitFilm:
    default:
        return HostEdition::HitFilm;
    }
}

enum class LicenseStatus
{
    Unknown = 0,
    Valid,
    Expired,
    NotFound,
    ActivationRequired,
};

enum class LicenseType
{
    Free   = 0,
    Retail,
    Subscription,
};

struct LicenseInfo
{
    LicenseStatus status = LicenseStatus::Unknown;
    LicenseType type = LicenseType::Free;
    HostEdition edition = HostEdition::Free;
    QString userName;
    QString productName;
    QString entitlementId;
};

QString toString(HostEdition edition);
QString toString(LicenseStatus status);

} // namespace license
} // namespace openvegas