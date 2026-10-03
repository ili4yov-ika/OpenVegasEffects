#pragma once

#include <QString>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <memory>

#include "composition/Composition.h"
#include "core/Result.h"
#include "media/MediaManager.h"

namespace openvegas {
namespace project {

struct ProjectSaveOptions {
    bool useRelativePaths = false;
    QByteArray screenLayout;
    // Set for an auto-save: the project it was taken from (empty while the
    // project is untitled), stored as Project/OpenVegasAutoSaveOf so a
    // recovered auto-save knows where to save back to.
    QString autoSaveOf;
    bool isAutoSave = false;
};

// Reads/writes the native ".vegfx" project format (VEGAS Effects / HitFilm-style
// XML). The document root is <VegasEffectsProject> containing a single <Project>
// with an <AssetList> (composition + media assets) and a primary <CompositionAsset>
// whose <Layers> hold TextLayer/AssetLayer instances. Layers are wrapped inside
// an element named after the layer itself (e.g. <1_foo.png>), which carries the
// real fields (StartFrame/EndFrame/BlendMode/...).
// A composite shot a file offers for import.
struct CompositeShotInfo {
    QString id;
    QString name;
    QSize size;
    double durationSeconds = 0.0;
};

class VegfxSerializer
{
public:
    static core::Result loadFromFile(const QString& filePath,
                                     composition::Composition* composition,
                                     media::MediaManager* media,
                                     QByteArray* screenLayout = nullptr);

    static core::Result saveToFile(const QString& filePath,
                                   const composition::Composition& composition,
                                   const media::MediaManager& media,
                                   const ProjectSaveOptions& options = {});

    // Composite shot files (.vegfxcs, the reference's FUN_1402c8b10): a
    // <BiffCompositeShot Version="1" AppEdition AppVersion> root holding the
    // media the shot uses in <Assets>, then the shot's own CompositionAsset.

    // The shots `filePath` holds: every CompositionAsset of a .vegfx, the one
    // of a .vegfxcs.
    static core::Result listCompositeShots(const QString& filePath,
                                           QVector<CompositeShotInfo>* shots);
    // Reads the shots `ids` from `filePath` - with any shot they nest - and
    // adds the media they use to `media`, ready to join a project. A shot
    // whose ID is in `takenIds` gets a new one, and the layers nesting it
    // follow.
    static core::Result importCompositeShots(
        const QString& filePath, const QStringList& ids, const QSet<QString>& takenIds,
        media::MediaManager* media, QVector<std::shared_ptr<composition::Composition>>* shots);
    // Writes `shot` as a .vegfxcs. A shot that nests another cannot be
    // written this way, as in the reference ("This composite shot cannot be
    // saved because it contains one or more embedded composite shots.").
    static core::Result saveCompositeShot(const QString& filePath,
                                          const composition::Composition& shot,
                                          const media::MediaManager& media,
                                          const ProjectSaveOptions& options = {});
};

} // namespace project
} // namespace openvegas
