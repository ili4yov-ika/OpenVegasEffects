#pragma once

#include <QDomDocument>

namespace openvegas {
namespace project {

// Lossless saving of a project that was opened from a file.
//
// The serializer writes what this port models. A .vegfx written by VEGAS
// Effects holds much more - layer materials, shadow and ambient-occlusion
// settings, text formats, viewer state, bin folders, metadata, the editor's
// clip objects - and writing only the model would throw all of that away on a
// plain open-and-save.
//
// mergeIntoSource() therefore starts from the document as it was read and
// lays the freshly generated one over it:
//  * children the writer is the authority for replace the stored ones;
//  * containers (Project, CompositionAsset, layer wrappers, LayerBase, ...)
//    are merged child by child;
//  * keyed lists follow the model - assets and layers by <ID>, editor tracks
//    by <ID>, properties by <Name> - so a layer or composite shot deleted in
//    the port goes;
//  * anything else the stored document has is kept as it was;
//  * the port's own OpenVegas* extensions are always rewritten, never kept
//    from the stored copy, so removed masks or clips do not come back.
// `source` is changed in place and returned.
QDomDocument mergeIntoSource(QDomDocument source, const QDomDocument& generated);

} // namespace project
} // namespace openvegas
