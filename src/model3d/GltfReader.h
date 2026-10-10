#pragma once

#include <QString>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace model3d {

// Reads a glTF 2.0 model - JSON (.gltf, its buffers and images beside it or
// in data: URIs) or binary (.glb) - which the reference offers in both of its
// import filters ("GL Transmission Format (*.gltf)", "... Binary (*.glb)").
//
// The scene's nodes become the model's nodes, nested as the file nests them,
// each one's geometry placed by its matrix or translation/rotation/scale; the
// triangles of every mesh primitive (mode 4) are read with their normals and
// first UV set. Materials are physically based in glTF and Phong here: the
// base colour is the diffuse colour, metallic tints the highlight with it and
// roughness widens it; the base colour, normal, emissive and occlusion
// textures are read (embedded in the buffer, in data: URIs or beside the
// file). Animations of the nodes' translation, rotation and scale are read,
// each on its own, with linear, step and cubic-spline keys, and skins: a
// skinned mesh's points (JOINTS_0 / WEIGHTS_0) follow their joints through
// the inverse bind matrices, and morph targets with their default and
// animated weights. Extensions are not read.
LoadResult loadGltf(const QString& filePath);

} // namespace model3d
} // namespace openvegas
