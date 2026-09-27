#pragma once

#include <QString>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace model3d {

// Reads an Alembic (.abc) archive into a flat mesh.
//
// The reference lists Alembic in both of its import filters - "Alembic (*.abc)"
// appears in the 3D model dialog and again in the animation one - and its own
// sample file is an .abc (D:\runningMan.abc), so this is a first-class format
// there rather than an afterthought.
//
// Only compiled when the vendored library in thirdparty/alembic is built, which
// CMake decides: Alembic needs Imath, and OPENVEGAS_HAVE_ALEMBIC is defined
// only when both are available. loadModel() reports .abc as unreadable
// otherwise rather than pretending.
//
// Every IPolyMesh in the archive is flattened into one mesh at its first
// sample. Animated topology and the per-object transforms of the hierarchy are
// not read: what a Model3DLayer shows is one piece of geometry, and the
// reference keeps animation in a Model3DAnimationAsset of its own.
LoadResult loadAlembic(const QString& filePath);

} // namespace model3d
} // namespace openvegas
