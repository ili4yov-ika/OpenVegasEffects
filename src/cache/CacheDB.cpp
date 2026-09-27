#include "cache/CacheDB.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlResult>

namespace openvegas {
namespace cache {

CacheDB::~CacheDB()
{
    close();
}

core::Result CacheDB::open(const QString& sqlitePath)
{
    if (m_db.isOpen()) {
        return core::Result::ok();
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("opengvegas_cache"));
    if (!m_db.isValid()) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("SQLite driver unavailable"));
    }

    m_db.setDatabaseName(sqlitePath);
    if (!m_db.open()) {
        const QString msg = m_db.lastError().text();
        QSqlDatabase::removeDatabase(QStringLiteral("opengvegas_cache"));
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Could not open cache database: %1").arg(msg));
    }

    QSqlQuery query(m_db);
    const QString schema = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cache_entries ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  key TEXT NOT NULL UNIQUE,"
        "  cache_file TEXT NOT NULL,"
        "  width INTEGER NOT NULL DEFAULT 0,"
        "  height INTEGER NOT NULL DEFAULT 0,"
        "  created_utc_ms INTEGER NOT NULL,"
        "  last_used_utc_ms INTEGER NOT NULL"
        ")");
    if (!query.exec(schema)) {
        const QString msg = query.lastError().text();
        close();
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Could not init cache schema: %1").arg(msg));
    }
    return core::Result::ok();
}

void CacheDB::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    if (m_db.isValid()) {
        const QString name = m_db.connectionName();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

bool CacheDB::isOpen() const
{
    return m_db.isOpen();
}

core::Result CacheDB::put(const CacheEntry& entry)
{
    if (!isOpen()) {
        return core::Result::fail(core::ResultStatus::OperationFailed, QStringLiteral("Cache is not open"));
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO cache_entries (key, cache_file, width, height, created_utc_ms, last_used_utc_ms) "
        "VALUES (:k, :f, :w, :h, :c, :u)"));
    query.bindValue(QStringLiteral(":k"), entry.key);
    query.bindValue(QStringLiteral(":f"), entry.cacheFile);
    query.bindValue(QStringLiteral(":w"), entry.width);
    query.bindValue(QStringLiteral(":h"), entry.height);
    query.bindValue(QStringLiteral(":c"), entry.createdUtcMs);
    query.bindValue(QStringLiteral(":u"), entry.lastUsedUtcMs);

    if (!query.exec()) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Insert failed: %1").arg(query.lastError().text()));
    }
    return core::Result::ok();
}

bool CacheDB::get(const QString& key, CacheEntry& out) const
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, key, cache_file, width, height, created_utc_ms, last_used_utc_ms "
        "FROM cache_entries WHERE key = :k"));
    query.bindValue(QStringLiteral(":k"), key);
    if (!query.exec() || !query.next()) {
        return false;
    }

    out.id = query.value(0).toLongLong();
    out.key = query.value(1).toString();
    out.cacheFile = query.value(2).toString();
    out.width = query.value(3).toInt();
    out.height = query.value(4).toInt();
    out.createdUtcMs = query.value(5).toLongLong();
    out.lastUsedUtcMs = query.value(6).toLongLong();
    return true;
}

bool CacheDB::removeByKey(const QString& key)
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM cache_entries WHERE key = :k"));
    query.bindValue(QStringLiteral(":k"), key);
    return query.exec();
}

qint64 CacheDB::pruneOlderThan(qint64 lastUsedBeforeUtcMs)
{
    if (!isOpen()) {
        return 0;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM cache_entries WHERE last_used_utc_ms < :t"));
    query.bindValue(QStringLiteral(":t"), lastUsedBeforeUtcMs);
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

qint64 CacheDB::purgeAll()
{
    if (!isOpen()) {
        return 0;
    }
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM cache_entries"))) {
        return 0;
    }
    return query.numRowsAffected();
}

qint64 CacheDB::entryCount() const
{
    if (!isOpen()) {
        return 0;
    }
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM cache_entries")) || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

qint64 CacheDB::totalBytes() const
{
    Q_UNUSED(this);
    return 0;
}

} // namespace cache
} // namespace openvegas