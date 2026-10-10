#pragma once

#include <QString>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace model3d {

// Reads an Autodesk FBX model (binary or ASCII, version 7), which the
// reference offers in both of its import filters ("Autodesk FilmBox (*.fbx)")
// and reads with the FBX SDK (FilmBoxModel3DImporter).
//
// Each Model becomes a node of the model, nested as the file connects them,
// placed by its Lcl Translation / Rotation (with its PreRotation) / Scaling;
// a Mesh geometry connected to it is read as polygons (fanned into
// triangles) with its normals and first UV set, its geometric transform
// applied, and the model's materials (diffuse, specular, emissive and
// ambient colours, shininess, opacity) with their textures - files beside
// the model or embedded. Animation stacks become the model's animations: the
// translation, rotation and scaling curves of its models, sampled at the
// reference's 24 frames a second. A skinned mesh (Skin deformer with its
// clusters) follows its bones, and blend shapes (BlendShape channels and
// their shapes) are morph targets, resting at DeformPercent and keyed by its
// curves. Models turn and scale about their pivots (rotation and scaling
// offsets and pivots, pre- and post-rotation), and animations are counted in
// the file's frame rate (TimeMode) and last as their stack says (LocalStop).
// As in the reference, the file's axes are not converted and its unit is
// not applied (there it only scales animation assets that ask for it).
LoadResult loadFbx(const QString& filePath);

} // namespace model3d
} // namespace openvegas
