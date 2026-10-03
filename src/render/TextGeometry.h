#pragma once

#include <QRectF>
#include <QVector>

#include "composition/TextStyle.h"
#include "model3d/Mesh.h"
#include "plugin/NativeEffectRender.h"

namespace openvegas {
namespace render {

// Geometry of a text layer as Flux hands it to Geometry modules: one batch per
// glyph (and one for the underline/strikethrough bars), each outline loop a
// double-sided polygon, nothing filled yet. Coordinates are the text box's
// with Y up and the box centre at the origin; z = 0.
//
// The loops follow what Extrude.hfpl expects of Flux's: the host normal points
// away from the viewer (the module turns the front copy towards it) and outer
// loops run clockwise seen from the front, holes the other way, so the walls it
// builds from each edge face outwards. extents[2..3] is the glyph's line, the
// shared vertical middle RotateGeometry turns glyphs about.
QVector<plugin::NativeGeometryBatch> textGeometry(const QRectF& box,
                                                  const composition::TextStyle& style);

// Replaces the polygons of every batch with triangles, the way Flux fills the
// geometry after the last module (FUN_1804d7ac0): the loops of one side and
// material are projected onto their plane and filled together, so a hole loop
// cuts its glyph. Loops that are neither front nor back are dropped.
void fillGeometryPolygons(QVector<plugin::NativeGeometryBatch>& geometry);

// The filled batches as one mesh, each batch placed by its matrix. Triangle
// materials index `materials`; anything outside uses the first.
model3d::Mesh meshFromGeometry(const QVector<plugin::NativeGeometryBatch>& geometry,
                               const QVector<model3d::Material>& materials);

} // namespace render
} // namespace openvegas
