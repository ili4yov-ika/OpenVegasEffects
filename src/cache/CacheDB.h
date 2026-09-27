#pragma once

#include <QSqlDatabase>
#include <QString>

#include "core/Result.h"

namespace openvegas {
namespace cache {

struct CacheEntry
{
    qint64 id = 0;
    QString key;
    QString cacheFile;
    int width = 0;
    int height = 0;
    qint64 createdUtcMs = 0;
    qint64 lastUsedUtcMs = 0;
};

class CacheDB
{
public:
    CacheDB() = default;
    ~CacheDB();

    CacheDB(const CacheDB&) = delete;
    CacheDB& operator=(const CacheDB&) = delete;

    core::Result open(const QString& sqlitePath);
    void close();

    bool isOpen() const;

    core::Result put(const CacheEntry& entry);
    bool get(const QString& key, CacheEntry& out) const;
    bool removeByKey(const QString& key);
    qint64 pruneOlderThan(qint64 lastUsedBeforeUtcMs);
    qint64 purgeAll();

    qint64 entryCount() const;
    qint64 totalBytes() const;

private:
    QSqlDatabase m_db;
};

} // namespace cache
} // namespace openvegas