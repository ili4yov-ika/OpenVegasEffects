#pragma once

#include <QString>
#include <QHashFunctions>

namespace openvegas {
namespace core {

class Identifier
{
public:
    Identifier() = default;

    explicit Identifier(const char* id)
        : m_id(QString::fromLatin1(id))
    {
    }

    explicit Identifier(const QString& id)
        : m_id(id)
    {
    }

    const QString& value() const { return m_id; }

    bool isValid() const { return !m_id.isEmpty(); }

    bool operator==(const Identifier& other) const { return m_id == other.m_id; }
    bool operator!=(const Identifier& other) const { return m_id != other.m_id; }
    bool operator<(const Identifier& other) const { return m_id < other.m_id; }

    friend size_t qHash(const Identifier& id, size_t seed = 0)
    {
        return qHash(id.m_id, seed);
    }

private:
    QString m_id;
};

} // namespace core
} // namespace openvegas