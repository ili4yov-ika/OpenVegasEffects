#include "media/AudioPlayer.h"
#include "media/VlcBackend.h"
#include "plugin/NativeEffectRender.h"
#include "core/Log.h"
#include <QDataStream>
#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <chrono>
#include <vector>

namespace openvegas::media {
namespace {
constexpr unsigned Rate = 48000, Channels = 2, BlockFrames = 480;
constexpr size_t QueueSamples = Rate * Channels + Channels * 2;
// Dry input kept per native effect for GetSampleRanges history requests:
// reverbs ask for up to their impulse length, Echo for delay * echoes.
constexpr qsizetype NativeHistoryFrames = qsizetype(1) << 20;
// How far ahead of the playhead native effects see the dry signal.
constexpr qint64 NativeLookAheadFrames = Rate / 4;
struct NativeDryHistory {
    QVector<qint16> samples;
    qint64 first = 0;
    qint64 end() const { return first + samples.size() / Channels; }
    void append(const QVector<qint16>& block, qint64 start) {
        if (samples.isEmpty() || end() != start) { samples.clear(); first = start; }
        samples.append(block);
        const qsizetype frames = samples.size() / Channels;
        if (frames > 2 * NativeHistoryFrames) {
            const qsizetype drop = frames - NativeHistoryFrames;
            samples.remove(0, drop * Channels); first += drop;
        }
    }
};
double meter(double squares, unsigned frames) {
    const double rms = std::sqrt(squares / std::max(1u, frames));
    return rms < 0.00001 ? 0 : std::clamp((20 * std::log10(rms) + 60) / 60, 0.0, 1.0);
}
}
struct AudioPlayer::Engine {
    struct Decoder {
        AudioClip clip;
        libvlc_media_player_t* player = nullptr;
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<qint16> pcm;
        std::atomic<bool> cancelled{false}, ended{false};
        double fraction = 0;
        ~Decoder() { shutdown(); }
        void shutdown() {
            cancelled = true; ready.notify_all();
            if (player) {
                vlc().libvlc_media_player_stop(player);
                vlc().libvlc_media_player_release(player); player = nullptr;
            }
        }
        static void decoded(void* opaque, const void* samples, unsigned count, int64_t) {
            auto& self = *static_cast<Decoder*>(opaque);
            const auto* data = static_cast<const qint16*>(samples);
            // A bounded producer queue; only the master consumes its PCM.
            std::unique_lock<std::mutex> lock(self.mutex);
            size_t remaining = size_t(count) * Channels;
            while (remaining && !self.cancelled) {
                self.ready.wait(lock, [&] { return self.cancelled || self.pcm.size() < QueueSamples; });
                if (self.cancelled) return;
                const size_t amount = std::min(remaining, QueueSamples - self.pcm.size());
                self.pcm.insert(self.pcm.end(), data, data + amount);
                data += amount; remaining -= amount; self.ready.notify_all();
            }
        }
        static void drained(void* opaque) {
            auto& self = *static_cast<Decoder*>(opaque);
            self.ended = true; self.ready.notify_all();
        }
        bool open(double timeline) {
            const auto& api = vlc();
            const QByteArray path = QDir::toNativeSeparators(QFileInfo(clip.path).absoluteFilePath()).toUtf8();
            auto* media = api.libvlc_media_new_path(vlcInstance(), path.constData());
            if (!media) { qWarning("VLC media creation failed: %s", api.libvlc_errmsg()); return false; }
            const double offset = std::max(0.0, clip.sourceStart + (timeline - clip.start) * clip.speed);
            const QByteArray start = QStringLiteral(":start-time=%1").arg(offset, 0, 'f', 9).toUtf8();
            api.libvlc_media_add_option(media, start.constData());
            api.libvlc_media_add_option(media, ":no-video");
            if (clip.audioStreamIndex >= 0) {
                const QByteArray track = QByteArray(":audio-track=") + QByteArray::number(clip.audioStreamIndex);
                api.libvlc_media_add_option(media, track.constData());
            }
            player = api.libvlc_media_player_new_from_media(media);
            api.libvlc_media_release(media);
            if (!player) { qWarning("VLC decoder creation failed: %s", api.libvlc_errmsg()); return false; }
            api.libvlc_audio_set_callbacks(player, decoded, nullptr, nullptr, nullptr, drained, this);
            api.libvlc_audio_set_format(player, "S16N", Rate, Channels);
            if (api.libvlc_media_player_play(player) != 0) { qWarning("VLC decode play failed: %s", api.libvlc_errmsg()); shutdown(); return false; }
            return true;
        }
        // Adds up to `frames` frames to output; returns how many were available.
        unsigned mix(float* output, unsigned frames) {
            std::unique_lock<std::mutex> lock(mutex);
            const double speed = std::clamp(clip.speed, 0.01, 100.0);
            const size_t needed = (size_t(std::ceil(fraction + frames * speed)) + 1) * Channels;
            ready.wait_for(lock, std::chrono::milliseconds(250), [&] {
                return cancelled || ended || pcm.size() >= std::min(needed, QueueSamples);
            });
            unsigned produced = 0;
            for (unsigned f = 0; f < frames; ++f, ++produced) {
                const size_t index = size_t(fraction) * Channels;
                if (index + Channels >= pcm.size()) break;
                const double blend = fraction - std::floor(fraction);
                for (unsigned c = 0; c < Channels; ++c)
                    output[f * Channels + c] += float(1.0 / 32768.0 *
                        ((1 - blend) * pcm[index + c] + blend * pcm[index + Channels + c]));
                fraction += speed;
            }
            const size_t consume = std::min(size_t(fraction), pcm.size() / Channels);
            for (size_t i = 0; i < consume * Channels; ++i) pcm.pop_front();
            fraction -= consume; ready.notify_all();
            return produced;
        }
    };
    // Dry clip PCM read ahead of the playhead for native effects, so modules
    // whose GetSampleRanges ask for later samples (NoiseReduction's analysis
    // window, DopplerShift) get them while playing. `next` is the timeline
    // sample of the first queued frame.
    struct NativeLookAhead {
        std::deque<float> samples;
        qint64 next = -1;
    };
    std::vector<NativeLookAhead> lookAhead;
    AudioPlayer* owner;
    quint64 generation;
    QVector<AudioClip> clips;
    QVector<AudioTransition> transitions;
    QSet<QString> failedNativeEffects;
    QHash<QString, NativeDryHistory> nativeHistory;
    std::vector<std::unique_ptr<Decoder>> decoders;
    libvlc_media_player_t* output = nullptr;
    std::atomic<bool> cancelled{false}, muted{false};
    quint64 frame = 0;
    double start = 0, duration = 0;
    QByteArray header, pending;
    qsizetype headerOffset = 0, pendingOffset = 0;
    std::mutex decodersMutex;
    std::mutex clockMutex;
    std::condition_variable clockWake;
    const std::chrono::steady_clock::time_point clockStart = std::chrono::steady_clock::now();
    Engine(AudioPlayer* parent, quint64 serial, QVector<AudioClip> sources, double time, double length, bool mute)
        : owner(parent), generation(serial), clips(std::move(sources)), muted(mute), start(time), duration(length) {
        decoders.resize(size_t(clips.size()));
        lookAhead.resize(size_t(clips.size()));
        QDataStream stream(&header, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(0xfffffff0);
        stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(Channels)
            << quint32(Rate) << quint32(Rate * Channels * 2) << quint16(Channels * 2) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(0xffffffcc);
    }
    ~Engine() { shutdown(); }
    void shutdown() {
        cancelled = true;
        clockWake.notify_all();
        {
            std::lock_guard<std::mutex> lock(decodersMutex);
            for (auto& decoder : decoders) if (decoder) {
                decoder->cancelled = true; decoder->ready.notify_all();
            }
        }
        // Join the reader before destroying any contexts it can access.
        if (output) {
            vlc().libvlc_media_player_stop(output);
            vlc().libvlc_media_player_release(output); output = nullptr;
        }
        for (auto& decoder : decoders) if (decoder) decoder->shutdown();
    }
    // Runs every transition overlapping the block over its part of the kept
    // clip inputs, adds the result and then whatever the clips play outside
    // their windows. A module that fails, or none runnable, cross-fades.
    void mixTransitions(float* master, unsigned frames, double now,
                        QHash<int, std::vector<float>>& inputs) {
        for (const AudioTransition& transition : std::as_const(transitions)) {
            const double first = std::max(now, transition.start);
            const double last = std::min(now + double(frames) / Rate, transition.end);
            if (last <= first) continue;
            const unsigned f0 = std::min(frames, unsigned(std::llround((first - now) * Rate)));
            const unsigned f1 = std::min(frames, unsigned(std::llround((last - now) * Rate)));
            if (f1 <= f0) continue;
            const unsigned count = f1 - f0;
            QVector<qint16> from(int(count * Channels), 0), to(int(count * Channels), 0);
            const auto take = [&](int clip, QVector<qint16>& target) {
                const auto it = inputs.find(clip);
                if (it == inputs.end()) return;
                for (unsigned s = 0; s < count * Channels; ++s) {
                    float& value = (*it)[f0 * Channels + s];
                    target[int(s)] = qint16(std::lround(std::clamp(double(value), -1.0, 1.0) * 32767.0));
                    value = 0.0f; // consumed by the transition
                }
            };
            take(transition.fromClip, from);
            take(transition.toClip, to);
            const qint32 total = qint32(std::max<qint64>(1, std::llround((transition.end - transition.start) * Rate)));
            const qint32 position = qint32(std::llround((first - transition.start) * Rate));
            const qint32 cut = qint32(std::llround((transition.cut - transition.start) * Rate));
            QVector<qint16> mixed;
            const QString key = QStringLiteral("transition:") + transition.pluginId.value();
            bool ok = !failedNativeEffects.contains(key)
                && plugin::nativeAudioTransitionRenderingVerified(transition.pluginId)
                && plugin::applyNativeAudioTransition(mixed, from, to, Channels, position, total,
                                                      transition.pluginId, transition.parameters, cut)
                && mixed.size() == from.size();
            if (!ok) {
                if (plugin::nativeAudioTransitionRenderingVerified(transition.pluginId))
                    failedNativeEffects.insert(key);
                mixed.resize(from.size());
                for (unsigned f = 0; f < count; ++f) {
                    const double w = std::clamp(double(position + qint32(f)) / total, 0.0, 1.0);
                    for (unsigned c = 0; c < Channels; ++c) {
                        const int s = int(f * Channels + c);
                        mixed[s] = qint16(std::lround(from[s] * (1.0 - w) + to[s] * w));
                    }
                }
            }
            for (unsigned s = 0; s < count * Channels; ++s)
                master[f0 * Channels + s] += float(mixed[int(s)] / 32768.0);
        }
        for (auto it = inputs.cbegin(); it != inputs.cend(); ++it)
            for (unsigned s = 0; s < frames * Channels; ++s) master[s] += (*it)[s];
    }
    static int opened(void* opaque, void** data, uint64_t* size) {
        *data = opaque; *size = UINT64_MAX; return 0;
    }
    static ptrdiff_t read(void* opaque, unsigned char* destination, size_t capacity) {
        auto& self = *static_cast<Engine*>(opaque);
        if (capacity == 0) return 0;
        if (self.cancelled) {
            plugin::releaseNativeEffectThreadRenderer();
            return 0;
        }
        if (self.headerOffset < self.header.size()) {
            const size_t n = std::min(capacity, size_t(self.header.size() - self.headerOffset));
            std::memcpy(destination, self.header.constData() + self.headerOffset, n);
            self.headerOffset += qsizetype(n); return ptrdiff_t(n);
        }
        if (self.pendingOffset >= self.pending.size()) {
            // A callback input has no external live clock. Bound read-ahead so
            // silence cannot fill the output pipeline hours ahead of playback.
            const auto due = self.clockStart + std::chrono::microseconds(
                qint64(std::max(0.0, double(self.frame) / Rate - 0.05) * 1000000));
            {
                std::unique_lock<std::mutex> lock(self.clockMutex);
                self.clockWake.wait_until(lock, due, [&] { return self.cancelled.load(); });
            }
            if (self.cancelled) {
                plugin::releaseNativeEffectThreadRenderer();
                return 0;
            }
            const double now = self.start + double(self.frame) / Rate;
            if (now >= self.duration) {
                plugin::releaseNativeEffectThreadRenderer();
                return 0;
            }
            const unsigned frames = unsigned(std::min(double(BlockFrames), std::ceil((self.duration - now) * Rate)));
            const double blockEnd = now + double(frames) / Rate;
            float samples[BlockFrames * Channels]{};
            // Clips inside an active transition are kept apart and combined by
            // it instead of being summed straight into the master.
            QHash<int, std::vector<float>> transitionInputs;
            for (const AudioTransition& transition : std::as_const(self.transitions)) {
                if (transition.end <= now || transition.start >= blockEnd) continue;
                for (int index : {transition.fromClip, transition.toClip}) {
                    if (index >= 0 && !transitionInputs.contains(index))
                        transitionInputs.insert(index, std::vector<float>(frames * Channels, 0.0f));
                }
            }
            for (int i = 0; i < self.clips.size() && !self.cancelled; ++i) {
                const AudioClip& clip = self.clips[i];
                const double first = std::max(now, clip.start), last = std::min(now + double(frames) / Rate, clip.end);
                if (last <= first) continue;
                Decoder* decoder;
                {
                    std::lock_guard<std::mutex> lock(self.decodersMutex);
                    if (self.cancelled) return 0;
                    auto& slot = self.decoders[size_t(i)];
                    if (!slot) continue;
                    decoder = slot.get();
                }
                const unsigned offset = std::min(frames, unsigned(std::llround((first - now) * Rate)));
                const unsigned count = std::min(frames - offset, unsigned(std::llround((last - first) * Rate)));
                float contribution[BlockFrames * Channels]{};
                QVector<qint16> futureSamples; // dry frames after this block
                if (clip.nativeEffects.isEmpty()) {
                    decoder->mix(contribution, count);
                } else {
                    NativeLookAhead& ahead = self.lookAhead[size_t(i)];
                    const qint64 firstSample = std::llround(first * Rate);
                    // Rounding may move a block start by a sample; only a real
                    // jump discards what was already read from the decoder.
                    if (std::abs(ahead.next - firstSample) > 2) ahead.samples.clear();
                    ahead.next = firstSample;
                    const qint64 queued = qint64(ahead.samples.size() / Channels);
                    const qint64 want = std::min<qint64>(std::llround(clip.end * Rate),
                                                         firstSample + count + NativeLookAheadFrames)
                                        - firstSample - queued;
                    if (want > 0) {
                        std::vector<float> pulled(size_t(want) * Channels, 0.0f);
                        const unsigned got = decoder->mix(pulled.data(), unsigned(want));
                        ahead.samples.insert(ahead.samples.end(), pulled.begin(),
                                             pulled.begin() + qsizetype(got) * Channels);
                    }
                    for (unsigned s = 0; s < count * Channels && !ahead.samples.empty(); ++s) {
                        contribution[s] = ahead.samples.front();
                        ahead.samples.pop_front();
                    }
                    ahead.next += count;
                    futureSamples.resize(qsizetype(ahead.samples.size()));
                    for (qsizetype s = 0; s < futureSamples.size(); ++s) {
                        futureSamples[s] = qint16(std::lround(
                            std::clamp(double(ahead.samples[size_t(s)]), -1.0, 1.0) * 32767.0));
                    }
                }
                QVector<qint16> nativeSamples;
                if (!clip.nativeEffects.isEmpty()) {
                    nativeSamples.resize(int(count * Channels));
                    for (unsigned sample = 0; sample < count * Channels; ++sample) {
                        nativeSamples[int(sample)] = qint16(std::lround(
                            std::clamp(double(contribution[sample]), -1.0, 1.0)
                            * 32767.0));
                    }
                    bool firstEffect = true;
                    for (const NativeAudioModule& effect : clip.nativeEffects) {
                        if (self.failedNativeEffects.contains(effect.instanceKey)) continue;
                        QStringList values = effect.parameters;
                        if (!effect.sourceEffect.animation.isEmpty()) {
                            const int parameterFrame = qRound(
                                (first - effect.shotOrigin)
                                * effect.shotRate * effect.shotFps);
                            for (auto it = effect.sourceEffect.animation.cbegin();
                                 it != effect.sourceEffect.animation.cend(); ++it) {
                                if (it.key() >= 0 && it.key() < values.size()) {
                                    values[it.key()] = effect.sourceEffect.parameterAt(
                                        it.key(), parameterFrame).toString();
                                }
                            }
                        }
                        const QVector<qint16> before = nativeSamples;
                        // History requests are exact while playing. The first
                        // effect also sees NativeLookAheadFrames of the dry
                        // future (NoiseReduction's window); later effects and
                        // whole-layer requests such as AudioReverse read
                        // silence there. Export is exact.
                        const qint64 layerSample = qRound64((first - effect.layerStart) * Rate);
                        NativeDryHistory& history = self.nativeHistory[effect.instanceKey];
                        history.append(nativeSamples, layerSample);
                        const plugin::NativeAudioLayer layer {
                            qRound64(effect.layerDuration * Rate), effect.shotFps};
                        // The future is appended for this call only, not copied
                        // with the (up to 2^21-frame) history.
                        const qsizetype historySize = history.samples.size();
                        if (firstEffect) history.samples.append(futureSamples);
                        firstEffect = false;
                        const bool applied = plugin::applyNativeAudioEffect(
                            nativeSamples, Channels, Rate, layerSample,
                            effect.pluginId, values, effect.instanceKey,
                            &history.samples, history.first, layer);
                        history.samples.resize(historySize);
                        if (!applied) {
                            nativeSamples = before;
                            self.failedNativeEffects.insert(effect.instanceKey);
                            qWarning().noquote()
                                << "Native HFPL realtime audio effect failed:"
                                << effect.pluginId.value();
                        }
                    }
                }
                const auto kept = transitionInputs.find(i);
                float* destination = kept != transitionInputs.end() ? kept->data() : samples;
                const bool enveloped = !clip.envelope.isEmpty();
                double gain = clip.gain;
                for (unsigned sample = 0; sample < count * Channels; ++sample) {
                    if (enveloped && sample % Channels == 0)
                        gain = clip.gainAt(first + double(sample / Channels) / Rate);
                    const double value = nativeSamples.isEmpty()
                        ? double(contribution[sample])
                        : double(nativeSamples[int(sample)]) / 32768.0;
                    destination[(offset * Channels) + sample] += float(value * gain);
                }
            }
            self.mixTransitions(samples, frames, now, transitionInputs);
            self.pending.resize(int(frames * Channels * 2)); self.pendingOffset = 0;
            double squares[Channels]{};
            for (unsigned i = 0; i < frames * Channels; ++i) {
                const double sample = self.muted ? 0 : std::clamp(double(samples[i]), -1.0, 1.0);
                squares[i % Channels] += sample * sample;
                qToLittleEndian<qint16>(qint16(std::lround(sample * 32767)), self.pending.data() + i * 2);
            }
            self.frame += frames;
            if (self.frame % (BlockFrames * 2) == 0) {
                const double left = meter(squares[0], frames), right = meter(squares[1], frames);
                const QByteArray block = self.pending;
                QMetaObject::invokeMethod(self.owner, [owner = self.owner, serial = self.generation, left, right, block, now] {
                    if (owner->m_generation == serial) {
                        emit owner->levelsChanged(left, right);
                        emit owner->pcmMixed(block, now);
                    }
                }, Qt::QueuedConnection);
            }
        }
        const size_t n = std::min(capacity, size_t(self.pending.size() - self.pendingOffset));
        std::memcpy(destination, self.pending.constData() + self.pendingOffset, n);
        self.pendingOffset += qsizetype(n); return ptrdiff_t(n);
    }
    bool begin() {
        const auto& api = vlc();
        // Create players outside VLC's input callbacks. Module initialization
        // can hold VLC locks while invoking read(), so re-entering the player
        // API there deadlocks. Future clips keep one bounded prefetch buffer.
        for (int i = 0; i < clips.size(); ++i) {
            if (clips[i].end <= start) continue;
            auto decoder = std::make_unique<Decoder>(); decoder->clip = clips[i];
            if (!decoder->open(std::max(start, clips[i].start))) decoder->ended = true;
            decoders[size_t(i)] = std::move(decoder);
        }
        auto* media = api.libvlc_media_new_callbacks(vlcInstance(), opened, read, nullptr, nullptr, this);
        if (!media) return false;
        api.libvlc_media_add_option(media, ":demux=wav");
        api.libvlc_media_add_option(media, ":no-video");
        api.libvlc_media_add_option(media, ":file-caching=50");
        output = api.libvlc_media_player_new_from_media(media);
        api.libvlc_media_release(media);
        if (!output || api.libvlc_media_player_play(output) != 0) return false;
        api.libvlc_audio_set_volume(output, 100);
        return true;
    }
};
AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {
    auto* poll = new QTimer(this); poll->setInterval(20);
    connect(poll, &QTimer::timeout, this, [this] {
        if (!m_engine) return;
        // Control-plane queries stay outside VLC's PCM/read callbacks.
        for (const auto& decoder : m_engine->decoders) {
            if (!decoder || !decoder->player) continue;
            const int state = vlc().libvlc_media_player_get_state(decoder->player);
            if (state == VlcEnded || state == VlcStopped || state == VlcError) {
                decoder->ended = true; decoder->ready.notify_all();
            }
        }
        if (m_engine->output && vlc().libvlc_media_player_get_state(m_engine->output) == VlcError) {
            const QString message = QString::fromUtf8(vlc().libvlc_errmsg());
            stop(); emit playbackError(message.isEmpty() ? vlcDescription() : message);
        }
    });
    poll->start();
}
AudioPlayer::~AudioPlayer() { stop(); }
bool AudioPlayer::isAvailable() { return vlcInstance() != nullptr; }
void AudioPlayer::setSource(const QString& path) {
    if (m_source == path && m_clips.size() == 1) return;
    stop(); m_source = path; m_clips.clear(); m_duration = vlcMediaDurationSeconds(path);
    if (m_duration > 0) m_clips.append({path, 0, m_duration, 0, 1, 1});
    m_transitions.clear();
}
void AudioPlayer::setClips(const QVector<AudioClip>& clips, double duration,
                           const QVector<AudioTransition>& transitions) {
    stop(); m_source.clear(); m_clips = clips; m_transitions = transitions;
    m_duration = std::max(0.0, duration);
}
void AudioPlayer::play(double seconds) {
    stop(); m_position = std::clamp(seconds, 0.0, m_duration);
    if (m_clips.isEmpty() || m_position >= m_duration) return;
    if (!isAvailable()) { emit playbackError(vlcDescription()); return; }
    m_engine = std::make_unique<Engine>(this, m_generation, m_clips, m_position, m_duration, m_muted);
    m_engine->transitions = m_transitions;
    if (!m_engine->begin()) {
        const QString detail = QString::fromUtf8(vlc().libvlc_errmsg());
        stop(); const QString error = detail.isEmpty() ? vlcDescription() : detail;
        OV_LOG_WARN(error); emit playbackError(error);
    }
}
void AudioPlayer::pause() { m_position = position(); stop(); }
void AudioPlayer::stop() { ++m_generation; m_engine.reset(); emit levelsChanged(0, 0); }
void AudioPlayer::seek(double seconds) { if (m_engine) play(seconds); else m_position = seconds; }
void AudioPlayer::setMuted(bool value) { m_muted = value; if (m_engine) m_engine->muted = value; }
bool AudioPlayer::isPlaying() const {
    return m_engine && m_engine->output && vlc().libvlc_media_player_is_playing(m_engine->output);
}
double AudioPlayer::position() const {
    if (!m_engine || !m_engine->output) return m_position;
    return m_engine->start + std::max<libvlc_time_t>(0, vlc().libvlc_media_player_get_time(m_engine->output)) / 1000.0;
}
}
