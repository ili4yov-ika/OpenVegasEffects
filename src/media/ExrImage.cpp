#include "media/ExrImage.h"

#include <QFile>
#include <QImage>

#ifdef OPENVEGAS_HAVE_OPENEXR
#include <ImfHeader.h>
#include <ImfRgba.h>
#include <ImfRgbaFile.h>
#include <ImfStdIO.h>

#include <sstream>
#include <string>
#include <vector>
#endif

namespace openvegas {
namespace media {

bool exrSupported()
{
#ifdef OPENVEGAS_HAVE_OPENEXR
    return true;
#else
    return false;
#endif
}

#ifdef OPENVEGAS_HAVE_OPENEXR

bool writeExr(const QImage& image, const QString& filePath, QString* error)
{
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("No frame to write");
        }
        return false;
    }

    const QImage src = image.convertToFormat(QImage::Format_RGBA8888);
    const int w = src.width();
    const int h = src.height();

    std::vector<Imf::Rgba> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        const uchar* line = src.constScanLine(y);
        Imf::Rgba* out = pixels.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            // 8-bit sRGB-encoded source; store the normalised values as-is so
            // the file round-trips what the viewer showed.
            out[x].r = half(line[x * 4 + 0] / 255.0f);
            out[x].g = half(line[x * 4 + 1] / 255.0f);
            out[x].b = half(line[x * 4 + 2] / 255.0f);
            out[x].a = half(line[x * 4 + 3] / 255.0f);
        }
    }

    // OpenEXR's file-based constructor takes a const char* and opens it with
    // the ANSI CRT on Windows, which mangles non-Latin paths. Encode into
    // memory instead and let QFile write the bytes - Qt handles Unicode paths.
    std::string encoded;
    try {
        Imf::StdOSStream stream;
        {
            // The stream-based constructor takes a Header rather than plain
            // width/height, unlike the file-name convenience overload.
            const Imf::Header header(w, h);
            Imf::RgbaOutputFile file(stream, header, Imf::WRITE_RGBA);
            file.setFrameBuffer(pixels.data(), 1, static_cast<size_t>(w));
            file.writePixels(h);
        }
        encoded = stream.str();
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot open %1 for writing: %2")
                         .arg(filePath, out.errorString());
        }
        return false;
    }
    const qint64 written =
        out.write(encoded.data(), static_cast<qint64>(encoded.size()));
    if (written != static_cast<qint64>(encoded.size())) {
        if (error) {
            *error = QStringLiteral("Short write to %1: %2").arg(filePath, out.errorString());
        }
        return false;
    }
    return true;
}

#else // !OPENVEGAS_HAVE_OPENEXR

bool writeExr(const QImage&, const QString&, QString* error)
{
    if (error) {
        *error = QStringLiteral(
            "This build has no OpenEXR support. Reconfigure with "
            "-DOPENVEGAS_WITH_OPENEXR=ON to enable .exr export.");
    }
    return false;
}

#endif

} // namespace media
} // namespace openvegas
