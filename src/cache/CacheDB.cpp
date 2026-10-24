#include "cache/CacheDB.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlResult>

#include <atomic>

// The index of the media cache: one SQLite row per file the application
// keeps in its media cache folder - waveform peaks, decoded frames and
// conformed audio (AppMain prefixes their keys "peaks:", "frame:" and
// "conformed:"). A row holds the file's path, its size and when it was last
// used, which is what the two pruning policies need: Options' "days to keep"
// (pruneOlderThan) and the size cap (pruneToBytes, least recently used first).
// Removing an entry always deletes its file too.

namespace openvegas {
namespace cache {

CacheDB::~CacheDB()
{
    close();
}

// Opens (or creates) the database file and its table; opening an open
// database does nothing.
core::Result CacheDB::open(const QString& sqlitePath)
{
    if (m_db.isOpen()) {
        return core::Result::ok();
    }

    // A connection of its own: a shared name would make a second database
    // replace the first one's connection under it.
    static std::atomic<int> connections{0};
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("openvegas_cache_%1").arg(++connections));
    if (!m_db.isValid()) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("SQLite driver unavailable"));
    }

    m_db.setDatabaseName(sqlitePath);
    if (!m_db.open()) {
        const QString msg = m_db.lastError().text();
        close();
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
        "  last_used_utc_ms INTEGER NOT NULL,"
        "  bytes INTEGER NOT NULL DEFAULT 0"
        ")");
    if (!query.exec(schema)) {
        const QString msg = query.lastError().text();
        close();
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Could not init cache schema: %1").arg(msg));
    }
    // Databases made before entries had a size get the column.
    bool hasBytes = false;
    if (query.exec(QStringLiteral("PRAGMA table_info(cache_entries)"))) {
        while (query.next()) hasBytes = hasBytes || query.value(1).toString() == QLatin1String("bytes");
    }
    if (!hasBytes) {
        query.exec(QStringLiteral("ALTER TABLE cache_entries ADD COLUMN bytes INTEGER NOT NULL DEFAULT 0"));
    }
    return core::Result::ok();
}

// Closes the connection and unregisters it from Qt, so its name can be used
// again and no driver handle outlives the object.
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

// Adds an entry, or replaces the one with the same key (a file written again).
core::Result CacheDB::put(const CacheEntry& entry)
{
    if (!isOpen()) {
        return core::Result::fail(core::ResultStatus::OperationFailed, QStringLiteral("Cache is not open"));
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO cache_entries (key, cache_file, width, height, created_utc_ms, last_used_utc_ms, bytes) "
        "VALUES (:k, :f, :w, :h, :c, :u, :b)"));
    query.bindValue(QStringLiteral(":k"), entry.key);
    query.bindValue(QStringLiteral(":f"), entry.cacheFile);
    query.bindValue(QStringLiteral(":w"), entry.width);
    query.bindValue(QStringLiteral(":h"), entry.height);
    query.bindValue(QStringLiteral(":c"), entry.createdUtcMs);
    query.bindValue(QStringLiteral(":u"), entry.lastUsedUtcMs);
    query.bindValue(QStringLiteral(":b"), entry.bytes);

    if (!query.exec()) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Insert failed: %1").arg(query.lastError().text()));
    }
    return core::Result::ok();
}

// The entry of a key; false when there is none.
bool CacheDB::get(const QString& key, CacheEntry& out) const
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, key, cache_file, width, height, created_utc_ms, last_used_utc_ms, bytes "
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
    out.bytes = query.value(7).toLongLong();
    return true;
}

// Deletes the least recently used entries, and their files, until the
// recorded sizes add up to maxBytes at most; returns how many went.
qint64 CacheDB::pruneToBytes(qint64 maxBytes)
{
    if (!isOpen()) {
        return 0;
    }
    QSqlQuery total(m_db);
    if (!total.exec(QStringLiteral("SELECT COALESCE(SUM(bytes), 0) FROM cache_entries")) || !total.next()) {
        return 0;
    }
    qint64 bytes = total.value(0).toLongLong();
    if (bytes <= maxBytes) {
        return 0;
    }
    QSqlQuery oldest(m_db);
    if (!oldest.exec(QStringLiteral(
            "SELECT key, cache_file, bytes FROM cache_entries ORDER BY last_used_utc_ms ASC"))) {
        return 0;
    }
    // Rows are collected first and deleted after: deleting while the SELECT
    // is still being stepped through would disturb it.
    QStringList keys;
    while (bytes > maxBytes && oldest.next()) {
        keys << oldest.value(0).toString();
        const QString file = oldest.value(1).toString();
        if (!file.isEmpty()) QFile::remove(file);
        bytes -= oldest.value(2).toLongLong();
    }
    for (const QString& key : keys) removeByKey(key);
    return keys.size();
}

// Marks an entry used now, so the pruning keeps it longer; false when the
// key is unknown.
bool CacheDB::touch(const QString& key, qint64 nowUtcMs)
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE cache_entries SET last_used_utc_ms = :u WHERE key = :k"));
    query.bindValue(QStringLiteral(":u"), nowUtcMs);
    query.bindValue(QStringLiteral(":k"), key);
    return query.exec() && query.numRowsAffected() > 0;
}

// The files of the entries matching an SQL condition with a ":t" parameter
// bound to value, or of all entries for an empty condition.
QStringList CacheDB::filesWhere(const QString& condition, qint64 value) const
{
    QStringList files;
    if (!isOpen()) {
        return files;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT cache_file FROM cache_entries")
                  + (condition.isEmpty() ? QString() : QStringLiteral(" WHERE ") + condition));
    if (!condition.isEmpty()) query.bindValue(QStringLiteral(":t"), value);
    if (!query.exec()) {
        return files;
    }
    while (query.next()) files << query.value(0).toString();
    return files;
}

// Deletes an entry's row; its file is the caller's.
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

// Deletes the entries, and their files, not used since the given time;
// returns how many went.
qint64 CacheDB::pruneOlderThan(qint64 lastUsedBeforeUtcMs)
{
    if (!isOpen()) {
        return 0;
    }
    for (const QString& file : filesWhere(QStringLiteral("last_used_utc_ms < :t"), lastUsedBeforeUtcMs)) {
        if (!file.isEmpty()) QFile::remove(file);
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM cache_entries WHERE last_used_utc_ms < :t"));
    query.bindValue(QStringLiteral(":t"), lastUsedBeforeUtcMs);
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

// Empties the cache: every file and every row.
qint64 CacheDB::purgeAll()
{
    if (!isOpen()) {
        return 0;
    }
    for (const QString& file : filesWhere(QString(), 0)) {
        if (!file.isEmpty()) QFile::remove(file);
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

// The size of the cache on disk: the files that still exist, measured now
// rather than the sizes recorded when they were written.
qint64 CacheDB::totalBytes() const
{
    qint64 bytes = 0;
    for (const QString& file : filesWhere(QString(), 0)) {
        const QFileInfo info(file);
        if (info.isFile()) bytes += info.size();
    }
    return bytes;
}

} // namespace cache
} // namespace openvegas