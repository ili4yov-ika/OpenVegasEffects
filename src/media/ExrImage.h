#pragma once

#include <QString>

class QImage;

namespace openvegas {
namespace media {

// EXR frame writing. The reference ships the OpenEXR 3.1 family
// (OpenEXR-3_1.dll, OpenEXRCore, OpenEXRUtil, Imath, Iex, IlmThread) and
// exposes an "OpenEXR Export" command, so the Export panel offers an .exr
// preset. Qt has no EXR image plugin, so that preset only works when the
// application is built against OpenEXR.
//
// Availability is a build-time choice: configure with
//   -DOPENVEGAS_WITH_OPENEXR=ON
// which defines OPENVEGAS_HAVE_OPENEXR.

// True when this build can write EXR files.
bool exrSupported();

// Writes `image` as a half-float RGBA OpenEXR file. Returns false and fills
// `error` when the build has no OpenEXR support or the write fails.
bool writeExr(const QImage& image, const QString& filePath, QString* error = nullptr);

} // namespace media
} // namespace openvegas
