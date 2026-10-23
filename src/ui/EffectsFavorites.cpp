#include "ui/EffectsFavorites.h"

#include "app/Settings.h"

#include <QDataStream>
#include <QIODevice>
#include <QSettings>

#include <memory>

// The Effects panel's Favorites and Recents, kept where and as the reference
// keeps them: group Plugins of the settings, each list a QDataStream (Qt 5.11
// format) of an int 0, the count and the plugin IDs - so the lists carry over
// between the two programs.
namespace openvegas {
namespace ui {
namespace {

const QString kGroup = QStringLiteral("Plugins");
const QString kFavorites = QStringLiteral("Favorites");
const QString kRecents = QStringLiteral("Recents");

std::unique_ptr<QSettings> settingsAt(const QString& path)
{
    if (path.isEmpty()) return std::make_unique<QSettings>(app::Settings::optionSettings().fileName(),
                                                           QSettings::IniFormat);
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
}

} // namespace

EffectsFavorites::EffectsFavorites(const QString& settingsPath, QObject* parent)
    : QObject(parent)
    , m_settingsPath(settingsPath)
{
    load();
}

QByteArray EffectsFavorites::encode(const QStringList& ids)
{
    QByteArray value;
    QDataStream stream(&value, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_11);
    stream << qint32(0) << qint32(ids.size());
    for (const QString& id : ids) stream << id;
    return value;
}

QStringList EffectsFavorites::decode(const QByteArray& value)
{
    QStringList ids;
    if (value.isEmpty()) return ids;
    QDataStream stream(value);
    stream.setVersion(QDataStream::Qt_5_11);
    qint32 version = 0, count = 0;
    stream >> version >> count;
    for (qint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QString id;
        stream >> id;
        if (stream.status() != QDataStream::Ok) break;
        if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
    }
    return ids;
}

void EffectsFavorites::load()
{
    const auto settings = settingsAt(m_settingsPath);
    settings->beginGroup(kGroup);
    m_favorites = decode(settings->value(kFavorites).toByteArray());
    m_recents = decode(settings->value(kRecents).toByteArray());
    settings->endGroup();
    while (m_recents.size() > kMaximumRecents) m_recents.removeFirst();
}

void EffectsFavorites::save(const QString& key, const QStringList& ids) const
{
    const auto settings = settingsAt(m_settingsPath);
    settings->beginGroup(kGroup);
    settings->setValue(key, encode(ids));
    settings->endGroup();
}

void EffectsFavorites::setFavorite(const QString& id, bool favorite)
{
    if (id.isEmpty() || isFavorite(id) == favorite) return;
    if (favorite) m_favorites.append(id);
    else m_favorites.removeAll(id);
    save(kFavorites, m_favorites);
    emit favoritesChanged();
}

// An effect just applied goes to the end of Recents (once; the newest is
// last) and the oldest drop off past kMaximumRecents.
void EffectsFavorites::addRecent(const QString& id)
{
    if (id.isEmpty() || (!m_recents.isEmpty() && m_recents.last() == id)) return;
    m_recents.removeAll(id);
    m_recents.append(id);
    while (m_recents.size() > kMaximumRecents) m_recents.removeFirst();
    save(kRecents, m_recents);
    emit recentsChanged();
}

void EffectsFavorites::clearRecents()
{
    if (m_recents.isEmpty()) return;
    m_recents.clear();
    save(kRecents, m_recents);
    emit recentsChanged();
}

} // namespace ui
} // namespace openvegas
