#pragma once

#include <QString>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace model3d {

// Reads an Autodesk 3D Studio model (.3ds), which the reference offers in
// its model import filter ("Autodesk 3ds Max (*.3ds)") and reads through
// Assimp (AssimpModel3DImporter).
//
// Each named object's triangle mesh becomes a node: its points (already in
// the scene's space), faces and texture coordinates, its faces' materials,
// and the materials themselves - ambient, diffuse and specular colours,
// shininess, transparency, self-illumination and the texture, opacity, bump
// and specular maps by file name beside the model. Materials and normals
// follow Assimp's 3DS loader, as the reference reads it: its defaults
// (diffuse 0.6, no specular, ambient or emission; faces without a material
// a grey of 0.3), a shininess (the shininess percentage times its strength)
// only for Phong or Metal shading, the scene's ambient light added to every
// ambient colour, and normals smoothed across each object's faces meeting at
// a point where their smoothing groups share a bit (a face in no group
// smooths with all of them). Objects nest as the keyframer arranges them
// (its object nodes, each under the node its header names, dummies by their
// instance names), objects it leaves out at the top; the geometry stays where
// the file puts it. Its animation is not read: the reference's Assimp
// importer has none.
LoadResult load3ds(const QString& filePath);

} // namespace model3d
} // namespace openvegas
