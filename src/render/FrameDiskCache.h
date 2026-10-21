#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QSize>
#include <QString>

namespace openvegas {
namespace render {

// Second level of the playback cache: rendered RGBA frames kept on disk under
// Options/TimelineCache, so a cached range survives the 256 MiB memory budget
// and the session. Files are zlib-compressed with a small header and named by
// the render signature; a frame is only ever read back for the exact same
// composition state, size and media. Used files are touched, so the
// TimelineCacheDays retention counts from the last use.
class FrameDiskCache
{
public:
    void setDirectory(const QString& directory) { m_directory = directory; }
    QString directory() const { return m_directory; }
    bool isEnabled() const { return !m_directory.isEmpty(); }

    bool load(const QString& key, const QSize& size, QByteArray* rgba) const;
    bool store(const QString& key, const QSize& size, const QByteArray& rgba) const;
    bool contains(const QString& key) const;
    // Removes frames unused for more than `days` (0 keeps everything) and
    // returns how many were deleted.
    int prune(int days, const QDateTime& now = QDateTime::currentDateTime()) const;

    // The reference's encode/decode debug squares (Debug > Show Hardware
    // Decoding Indicator, Options/ShowHardwareDecodingIndicator, which Flux
    // reads into Flux::SetUseEncodeDecodeDebugSquares): its viewer cache
    // draws a 200 px square into a frame as it encodes it - top left, red for
    // a hardware encoder, blue for FXEncoderH264 (VegasEffects.exe
    // FUN_140983790) - and as it decodes one - top right, dark red for a
    // hardware decoder, dark blue for FXVideoFrameDecoder (FUN_140984a10).
    // This cache's codec is software (zlib), so a frame stored gets the blue
    // square and a frame read back the dark blue one.
    static void setDebugSquares(bool on);
    static bool debugSquares();

private:
    QString pathFor(const QString& key) const;
    QString m_directory;
};

} // namespace render
} // namespace openvegas
