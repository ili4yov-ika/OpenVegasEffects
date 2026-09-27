#pragma once

#include <QString>
#include <QVersionNumber>

namespace openvegas {
namespace core {

class Version
{
public:
    Version() = default;

    Version(int major, int minor, int patch)
        : m_version(QVersionNumber(major, minor, patch))
    {
    }

    static Version fromString(const QString& s)
    {
        Version v;
        v.m_version = QVersionNumber::fromString(s);
        return v;
    }

    int major() const { return m_version.majorVersion(); }
    int minor() const { return m_version.minorVersion(); }
    int patch() const { return m_version.segmentCount() > 2 ? m_version.segmentAt(2) : 0; }

    QString toString() const { return m_version.toString(); }

    bool operator<(const Version& other) const { return m_version < other.m_version; }

private:
    QVersionNumber m_version{0, 0, 0};
};

} // namespace core
} // namespace openvegas