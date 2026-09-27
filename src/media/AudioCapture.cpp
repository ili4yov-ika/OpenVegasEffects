#include "media/AudioCapture.h"
#include <QMutexLocker>
#include <QProcess>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dshow.h>
#elif defined(Q_OS_MACOS)
#include <CoreAudio/CoreAudio.h>
#endif
namespace openvegas::media {
QVector<AudioInputDevice> audioInputDevices() {
    QVector<AudioInputDevice> devices;
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ICreateDevEnum* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, reinterpret_cast<void**>(&enumerator)))) {
        IEnumMoniker* monikers = nullptr;
        if (enumerator->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &monikers, 0) == S_OK) {
            IMoniker* moniker = nullptr;
            while (monikers->Next(1, &moniker, nullptr) == S_OK) {
                IPropertyBag* bag = nullptr;
                if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&bag)))) {
                    VARIANT name; VariantInit(&name);
                    if (SUCCEEDED(bag->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR) {
                        const QString text = QString::fromWCharArray(name.bstrVal);
                        devices.append({QStringLiteral("dshow:") + text, text});
                    }
                    VariantClear(&name); bag->Release();
                }
                moniker->Release();
            }
            monikers->Release();
        }
        enumerator->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
#elif defined(Q_OS_MACOS)
    AudioObjectPropertyAddress property{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &property, 0, nullptr, &size) == noErr) {
        QVector<AudioDeviceID> ids(int(size / sizeof(AudioDeviceID)));
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &property, 0, nullptr, &size, ids.data()) == noErr)
            for (AudioDeviceID id : ids) {
                property = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain};
                UInt32 streams = 0;
                if (AudioObjectGetPropertyDataSize(id, &property, 0, nullptr, &streams) != noErr || !streams) continue;
                auto text = [id](AudioObjectPropertySelector selector) {
                    AudioObjectPropertyAddress key{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                    CFStringRef value = nullptr; UInt32 bytes = sizeof(value);
                    if (AudioObjectGetPropertyData(id, &key, 0, nullptr, &bytes, &value) != noErr || !value) return QString();
                    const QString result = QString::fromCFString(value); CFRelease(value); return result;
                };
                devices.append({QStringLiteral("qtsound:") + text(kAudioDevicePropertyDeviceUID), text(kAudioObjectPropertyName)});
            }
    }
#else
    // PulseAudio and PipeWire's Pulse compatibility expose the same source IDs.
    QProcess process;
    process.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sources")});
    if (process.waitForFinished(1500) && process.exitCode() == 0)
        for (const auto& line : QString::fromUtf8(process.readAllStandardOutput()).split('\n')) {
            const auto columns = line.split('\t');
            if (columns.size() > 1 && !columns[1].isEmpty())
                devices.append({QStringLiteral("pulse:") + columns[1], columns[1]});
        }
#endif
    return devices;
}
bool AudioCapture::start(const QString& id, const PcmFormat& format, QIODevice* destination) {
    stop(); m_failed = false;
    if (!format.isValid() || format.sampleFormat() != PcmFormat::Int16 || !destination || !vlcInstance()) return false;
    const auto devices = audioInputDevices();
    if (id != QLatin1String("default") && std::none_of(devices.begin(), devices.end(), [&](const auto& device) { return device.id == id; })) return false;
    QString location;
    QStringList options{QStringLiteral(":no-video"), QStringLiteral(":live-caching=50")};
#ifdef Q_OS_WIN
    location = QStringLiteral("dshow://");
    options << QStringLiteral(":dshow-vdev=none")
            << QStringLiteral(":dshow-adev=%1").arg(id == QLatin1String("default") ? QString() : id.mid(6));
#elif defined(Q_OS_MACOS)
    location = QStringLiteral("qtsound://") + (id == QLatin1String("default") ? QString() : id.mid(8));
#else
    location = QStringLiteral("pulse://") + (id == QLatin1String("default") ? QString() : id.mid(6));
#endif
    const auto& api = vlc();
    auto* media = api.libvlc_media_new_location(vlcInstance(), location.toUtf8().constData());
    if (!media) return false;
    for (const auto& option : options) api.libvlc_media_add_option(media, option.toUtf8().constData());
    m_player = api.libvlc_media_player_new_from_media(media); api.libvlc_media_release(media);
    if (!m_player) return false;
    m_format = format; m_destination = destination;
    api.libvlc_audio_set_callbacks(m_player, samples, nullptr, nullptr, nullptr, nullptr, this);
    api.libvlc_audio_set_format(m_player, "S16N", unsigned(format.sampleRate()), unsigned(format.channelCount()));
    if (api.libvlc_media_player_play(m_player) != 0) { stop(); return false; }
    return true;
}
void AudioCapture::stop() {
    if (m_player) {
        vlc().libvlc_media_player_stop(m_player);
        vlc().libvlc_media_player_release(m_player); m_player = nullptr;
    }
    QMutexLocker lock(&m_mutex); m_destination = nullptr;
}
bool AudioCapture::failed() const {
    return m_failed || (m_player && vlc().libvlc_media_player_get_state(m_player) == VlcError);
}
void AudioCapture::samples(void* opaque, const void* input, unsigned frames, int64_t) {
    auto& self = *static_cast<AudioCapture*>(opaque);
    const size_t count = size_t(frames) * self.m_format.channelCount();
    if (count > size_t(std::numeric_limits<int>::max() / 2)) { self.m_failed = true; return; }
    QByteArray bytes(int(count * 2), Qt::Uninitialized);
    const auto* pcm = static_cast<const qint16*>(input);
    const double volume = std::clamp(self.m_volume.load(), 0.0, 1.0);
    for (size_t i = 0; i < count; ++i) qToLittleEndian<qint16>(qint16(std::lround(pcm[i] * volume)), bytes.data() + i * 2);
    QMutexLocker lock(&self.m_mutex);
    if (self.m_destination && self.m_destination->write(bytes) != bytes.size()) self.m_failed = true;
}
}
