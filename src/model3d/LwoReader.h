#pragma once

#include <QString>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace model3d {

// Reads a NewTek LightWave 3D object (.lwo: the LWO2 form, and LightWave
// 5's LWOB), which the reference offers in its model import filter
// ("NewTek LightWave 3D (*.lwo)") and reads through Assimp
// (AssimpModel3DImporter), whose rules this follows.
//
// Each layer becomes a node, nested by its parent layer; its points (turned
// from LightWave's left-handed space into the port's right-handed one) and
// face polygons (LWOB's detail polygons too), fanned into triangles, with
// the first UV map (per point, and per polygon corner where the object
// breaks it) and each polygon's surface. Normals are made per surface as
// Assimp's loader makes them: smoothed between faces up to the surface's
// smoothing angle (SMAN; LWOB's smoothing flag is 89.5 degrees), faceted
// without one. Surfaces become materials as the reference reads Assimp's:
// colour times diffuse; where there is both specularity and glossiness, a
// white highlight (towards the colour by its colour highlights) with a
// shininess of the glossiness exponent ((10 g + 2) squared; LWOB's
// glossiness in four steps) times the specularity, else none; 0.8 of the
// luminosity as emission; one less the transparency as opacity. The images
// of UV-mapped colour (or diffuse), specular and bump blocks are kept.
// Envelopes and animation (Assimp's importer has none in the reference),
// procedural textures and non-UV projections (all of LWOB's textures) are
// not read.
LoadResult loadLwo(const QString& filePath);

} // namespace model3d
} // namespace openvegas
