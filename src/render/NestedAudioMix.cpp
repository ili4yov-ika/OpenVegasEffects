#include "render/NestedAudioMix.h"

#include "composition/Composition.h"
#include "core/Log.h"
#include "media/MediaManager.h"
#include "render/AudioExport.h"
#include "render/RenderManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace openvegas {
namespace render {

NestedAudioMixCache::NestedAudioMixCache(QObject* parent)
    : QObject(parent)
{
}

NestedAudioMixCache::~NestedAudioMixCache()
{
    for (const QPointer<QThread>& thread : std::as_const(m_threads))
        if (thread) thread->wait();
}

QString NestedAudioMixCache::pathFor(const composition::Composition& shot, const media::MediaManager* media) const
{
    if (m_directory.isEmpty()) return {};
    const QString state = renderStateKey(shot, media, 0.0, QSize(qMax(1, shot.width()), qMax(1, shot.height())),
                                         true, true, QStringLiteral("audio-mix"));
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(state.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QDir(m_directory).filePath(key + QStringLiteral(".wav"));
}

QString NestedAudioMixCache::mixFor(const std::shared_ptr<composition::Composition>& shot,
                                    const std::shared_ptr<media::MediaManager>& media)
{
    if (!shot) return {};
    const QString path = pathFor(*shot, media.get());
    if (path.isEmpty() || m_silent.contains(path)) return {};
    if (QFileInfo(path).isFile() && QFileInfo(path).size() > 44) return path;
    if (m_building.contains(path)) return {};
    m_building.insert(path);
    // The shot as it is now, mixed away from the interface.
    auto snapshot = std::make_shared<composition::Composition>(*shot);
    const QString preRenders = m_preRenders;
    const QString id = shot->id().value();
    QPointer<NestedAudioMixCache> self(this);
    QThread* thread = QThread::create([snapshot, media, preRenders, path, id, self] {
        QString error;
        const bool ok = writeCompositionAudio(*snapshot, media.get(), preRenders, path, &error);
        if (!ok) OV_LOG_WARN(QStringLiteral("Mix of nested shot %1: %2").arg(id, error));
        if (NestedAudioMixCache* cache = self.data()) {
            QMetaObject::invokeMethod(cache, [self, path, id, ok] {
                if (!self) return;
                self->m_building.remove(path);
                if (!ok || !QFileInfo(path).isFile()) {
                    self->m_silent.insert(path);
                    return;
                }
                emit self->mixReady(id);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_threads.removeAll(nullptr);
    m_threads.append(thread);
    thread->start();
    return {};
}

} // namespace render
} // namespace openvegas
