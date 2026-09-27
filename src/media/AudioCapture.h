#pragma once
#include "media/PcmWave.h"
#include "media/VlcBackend.h"
#include <QIODevice>
#include <QMutex>
#include <QVector>
#include <atomic>
namespace openvegas::media {
struct AudioInputDevice { QString id, description; };
QVector<AudioInputDevice> audioInputDevices();
// The destination outlives capture. stop() joins VLC callbacks before returning.
class AudioCapture {
public:
    ~AudioCapture() { stop(); }
    bool start(const QString& device, const PcmFormat& format, QIODevice* destination);
    void stop();
    void setVolume(double value) { m_volume = value; }
    bool failed() const;
private:
    static void samples(void*, const void*, unsigned, int64_t);
    libvlc_media_player_t* m_player = nullptr;
    QIODevice* m_destination = nullptr;
    PcmFormat m_format;
    QMutex m_mutex;
    std::atomic<double> m_volume{1};
    std::atomic<bool> m_failed{false};
};
}
