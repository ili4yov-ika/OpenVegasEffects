#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace openvegas {
namespace ui {

// The Effects panel's favourite and recent effects, as the reference's
// biff::ui::effects::EffectsFavoritesManager keeps them (VegasEffects.exe
// 1406743e0 / 1406746b0 read, 140674980 writes): the settings group
// "Plugins", keys "Favorites" and "Recents", each a QByteArray written by a
// QDataStream (Qt 5.11 format) holding int 0, the count and the effects' IDs
// as strings. Recents (140674fd0) holds the last five effects used, the
// newest last: one used again moves to the end, a sixth drops the oldest.
class EffectsFavorites : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaximumRecents = 5;

    // An empty path keeps them in the application's settings.
    explicit EffectsFavorites(const QString& settingsPath = QString(), QObject* parent = nullptr);

    QStringList favorites() const { return m_favorites; }
    QStringList recents() const { return m_recents; }
    bool isFavorite(const QString& id) const { return m_favorites.contains(id); }

    void setFavorite(const QString& id, bool favorite);
    void toggleFavorite(const QString& id) { setFavorite(id, !isFavorite(id)); }
    void addRecent(const QString& id);
    void clearRecents();

    // The settings value of a list, and back (empty for anything else).
    static QByteArray encode(const QStringList& ids);
    static QStringList decode(const QByteArray& value);

signals:
    void favoritesChanged();
    void recentsChanged();

private:
    void load();
    void save(const QString& key, const QStringList& ids) const;

    QString m_settingsPath;
    QStringList m_favorites;
    QStringList m_recents;
};

} // namespace ui
} // namespace openvegas
