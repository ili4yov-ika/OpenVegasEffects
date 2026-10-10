#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

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
    qint64 bytes = 0;   // the cache file's size when it was written
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
    // Marks an entry as used now, so DaysToKeepMediaCacheFiles counts from here.
    bool touch(const QString& key, qint64 nowUtcMs);
    bool removeByKey(const QString& key);
    // Entries last used before the time go, and their cache files with them.
    qint64 pruneOlderThan(qint64 lastUsedBeforeUtcMs);
    // Every entry and its file.
    qint64 purgeAll();
    // The least recently used entries and their files go until the recorded
    // sizes add up to no more than `maxBytes`; returns how many went.
    qint64 pruneToBytes(qint64 maxBytes);

    qint64 entryCount() const;
    // The size of the cache files on disk.
    qint64 totalBytes() const;

private:
    // The files of the entries a WHERE clause picks (all for an empty one).
    QStringList filesWhere(const QString& condition, qint64 value) const;

    QSqlDatabase m_db;
};

} // namespace cache
} // namespace openvegas