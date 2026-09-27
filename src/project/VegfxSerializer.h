#pragma once

#include <QString>
#include <QMap>

#include "composition/Composition.h"
#include "core/Result.h"
#include "media/MediaManager.h"

namespace openvegas {
namespace project {

struct ProjectSaveOptions {
    bool useRelativePaths = false;
    QByteArray screenLayout;
};

// Reads/writes the native ".vegfx" project format (VEGAS Effects / HitFilm-style
// XML). The document root is <VegasEffectsProject> containing a single <Project>
// with an <AssetList> (composition + media assets) and a primary <CompositionAsset>
// whose <Layers> hold TextLayer/AssetLayer instances. Layers are wrapped inside
// an element named after the layer itself (e.g. <1_foo.png>), which carries the
// real fields (StartFrame/EndFrame/BlendMode/...).
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
};

} // namespace project
} // namespace openvegas
