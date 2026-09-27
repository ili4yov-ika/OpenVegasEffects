#include "model3d/AlembicReader.h"

#ifdef OPENVEGAS_HAVE_ALEMBIC

#include <QCoreApplication>
#include <QFileInfo>

#include <Alembic/Abc/All.h>
#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>

#include <exception>

namespace openvegas {
namespace model3d {

namespace {

namespace AbcG = Alembic::AbcGeom;

// Appends one polygon mesh to `mesh`, offsetting its indices past whatever is
// already there so several objects in one archive share the vertex pool.
void appendPolyMesh(const AbcG::IPolyMesh& polyMesh, Mesh* mesh)
{
    AbcG::IPolyMeshSchema schema = polyMesh.getSchema();
    if (schema.getNumSamples() == 0) {
        return;
    }

    AbcG::IPolyMeshSchema::Sample sample;
    // First sample: an animated mesh is read at the start of its own time
    // range, which is the state a still model layer shows.
    schema.get(sample, Alembic::Abc::ISampleSelector(Alembic::Abc::index_t(0)));

    Alembic::Abc::P3fArraySamplePtr positions = sample.getPositions();
    Alembic::Abc::Int32ArraySamplePtr faceIndices = sample.getFaceIndices();
    Alembic::Abc::Int32ArraySamplePtr faceCounts = sample.getFaceCounts();
    if (!positions || !faceIndices || !faceCounts) {
        return;
    }

    const int positionBase = mesh->positions.size();
    for (size_t i = 0; i < positions->size(); ++i) {
        const Imath::V3f& p = (*positions)[i];
        mesh->positions.append(QVector3D(p.x, p.y, p.z));
    }

    // Normals, when the archive carries them. Alembic stores them as their own
    // parameter rather than inside the sample.
    const int normalBase = mesh->normals.size();
    bool haveNormals = false;
    bool normalsAreIndexed = false;
    Alembic::Abc::N3fArraySamplePtr normalValues;
    Alembic::Abc::UInt32ArraySamplePtr normalIndices;
    AbcG::IN3fGeomParam normalParam = schema.getNormalsParam();
    if (normalParam.valid() && normalParam.getNumSamples() > 0) {
        AbcG::IN3fGeomParam::Sample normalSample;
        normalParam.getIndexed(normalSample, Alembic::Abc::ISampleSelector(Alembic::Abc::index_t(0)));
        normalValues = normalSample.getVals();
        normalIndices = normalSample.getIndices();
        if (normalValues) {
            haveNormals = true;
            normalsAreIndexed = normalIndices && normalIndices->size() > 0;
            for (size_t i = 0; i < normalValues->size(); ++i) {
                const Imath::V3f& n = (*normalValues)[i];
                mesh->normals.append(QVector3D(n.x, n.y, n.z));
            }
        }
    }

    const int uvBase = mesh->uvs.size();
    bool haveUVs = false;
    bool uvsAreIndexed = false;
    Alembic::Abc::V2fArraySamplePtr uvValues;
    Alembic::Abc::UInt32ArraySamplePtr uvIndices;
    AbcG::IV2fGeomParam uvParam = schema.getUVsParam();
    if (uvParam.valid() && uvParam.getNumSamples() > 0) {
        AbcG::IV2fGeomParam::Sample uvSample;
        uvParam.getIndexed(uvSample, Alembic::Abc::ISampleSelector(Alembic::Abc::index_t(0)));
        uvValues = uvSample.getVals();
        uvIndices = uvSample.getIndices();
        if (uvValues) {
            haveUVs = true;
            uvsAreIndexed = uvIndices && uvIndices->size() > 0;
            for (size_t i = 0; i < uvValues->size(); ++i) {
                const Imath::V2f& uv = (*uvValues)[i];
                mesh->uvs.append(QVector2D(uv.x, uv.y));
            }
        }
    }

    // Faces are arbitrary polygons: faceCounts says how many corners each has
    // and faceIndices is the flat run of them. Fan-triangulate, as the OBJ
    // reader does, so both formats reach the rasteriser the same way.
    //
    // Alembic winds its faces clockwise where the rest of this port is
    // counter-clockwise, so the corners after the first are taken in reverse.
    size_t corner = 0;
    for (size_t f = 0; f < faceCounts->size(); ++f) {
        const int count = (*faceCounts)[f];
        if (count < 3 || corner + size_t(count) > faceIndices->size()) {
            corner += size_t(qMax(0, count));
            continue;
        }
        const auto cornerAttributes = [&](size_t which, Triangle* tri, int slot) {
            const size_t flat = corner + which;
            tri->position[slot] = positionBase + int((*faceIndices)[flat]);
            if (haveNormals) {
                const size_t index = normalsAreIndexed && flat < normalIndices->size()
                    ? size_t((*normalIndices)[flat])
                    : flat;
                if (index < normalValues->size()) {
                    tri->normal[slot] = normalBase + int(index);
                }
            }
            if (haveUVs) {
                const size_t index = uvsAreIndexed && flat < uvIndices->size()
                    ? size_t((*uvIndices)[flat])
                    : flat;
                if (index < uvValues->size()) {
                    tri->uv[slot] = uvBase + int(index);
                }
            }
        };

        for (int i = 1; i + 1 < count; ++i) {
            Triangle tri;
            cornerAttributes(0, &tri, 0);
            cornerAttributes(size_t(i + 1), &tri, 1);
            cornerAttributes(size_t(i), &tri, 2);
            mesh->triangles.append(tri);
        }
        corner += size_t(count);
    }

    const QString name = QString::fromStdString(polyMesh.getName());
    if (!name.isEmpty() && !mesh->nodeNames.contains(name)) {
        mesh->nodeNames.append(name);
    }
}

// Depth-first walk of the archive, collecting every polygon mesh under `object`.
void collectMeshes(const Alembic::Abc::IObject& object, Mesh* mesh)
{
    for (size_t i = 0; i < object.getNumChildren(); ++i) {
        const Alembic::Abc::IObject child = object.getChild(i);
        if (AbcG::IPolyMesh::matches(child.getHeader())) {
            appendPolyMesh(AbcG::IPolyMesh(child, Alembic::Abc::kWrapExisting), mesh);
        }
        collectMeshes(child, mesh);
    }
}

} // namespace

LoadResult loadAlembic(const QString& filePath)
{
    LoadResult result;
    try {
        // The factory picks the backend by looking at the file, so an archive
        // written by either Ogawa or HDF5 opens without the caller choosing.
        Alembic::AbcCoreFactory::IFactory factory;
        Alembic::Abc::IArchive archive = factory.getArchive(filePath.toStdString());
        if (!archive.valid()) {
            result.message =
                QCoreApplication::translate("Model3D", "The file could not be opened.");
            return result;
        }
        collectMeshes(archive.getTop(), &result.mesh);
    } catch (const std::exception& error) {
        // Alembic reports a damaged or unexpected archive by throwing, and a
        // bad file must not take the application down with it.
        result.message = QString::fromUtf8(error.what());
        return result;
    }

    if (result.mesh.triangles.isEmpty()) {
        result.message = QCoreApplication::translate("Model3D", "The file contains no geometry.");
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace model3d
} // namespace openvegas

#endif // OPENVEGAS_HAVE_ALEMBIC
