#pragma once

#include <QString>
#include <QStringList>

#include <memory>

#include "model3d/Mesh.h"
#include "model3d/ModelImportSettings.h"

namespace openvegas {
namespace model3d {

// Outcome of a load. `mesh` is only meaningful when ok is true; `message`
// carries the reason otherwise, and is also filled on success when something
// was skipped (a texture that could not be found, say).
struct LoadResult
{
    bool ok = false;
    Mesh mesh;
    QString message;
};

// Formats the reference's import dialogs offer, verbatim from its own filter
// strings (1412d0000 and 1412cff50). Kept as data so the file dialog, the
// extension check and the "which of these can we actually read" report all
// agree.
QStringList modelExtensions();       // 3ds lwo obj fbx abc gltf glb
QStringList animationExtensions();   // fbx abc gltf glb
QString modelFileFilter();
QString animationFileFilter();
// The nodes and animations of an animation file, read once and kept while
// the file stays the same (safe from any thread); null when it cannot be
// read or holds no animation.
std::shared_ptr<const Mesh> animationSource(const QString& filePath);

// True when this build can actually parse the extension, as opposed to merely
// offering it in the dialog.
bool canReadExtension(const QString& suffix);
// Human-readable list of what this build reads, for the import error message.
QString readableFormats();

// Loads a model, applying `settings` to what comes off disk.
LoadResult loadModel(const QString& filePath, const ImportSettings& settings);

} // namespace model3d
} // namespace openvegas
