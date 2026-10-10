#pragma once

namespace openvegas::media {

inline constexpr int kDefaultAudioSampleRate = 48000;

// Bound buffers for malformed/foreign project values. All standard project
// rates from 8 to 192 kHz (including 44.1/88.2/176.4 kHz) pass through unchanged.
inline int normalizedAudioSampleRate(int rate)
{
    return rate >= 8000 && rate <= 192000 ? rate : kDefaultAudioSampleRate;
}

} // namespace openvegas::media
