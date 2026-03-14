#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ftp::host
{
struct HostTiming
{
    bool valid = false;
    bool isPlaying = false;
    bool isRecording = false;
    double bpm = 120.0;
    double ppqStart = 0.0;
    double projectTimeStartSec = 0.0;
    int numerator = 4;
    int denominator = 4;
    std::int64_t timeInSamples = 0;
};

struct GridMatch
{
    bool valid = false;
    double ppqAtEvent = 0.0;
    double nearestGridPpq = 0.0;
    double projectTimeSec = 0.0;
    double signedErrorMs = 0.0;
};

class TransportSync
{
public:
    HostTiming read(const juce::AudioPlayHead* playHead) const;
    GridMatch matchToGrid(const HostTiming& timing,
                          double eventAudioTimeSec,
                          double sampleRate,
                          double gridDivision) const noexcept;
    std::int32_t countBeatsCrossed(const HostTiming& timing, int numSamples, double sampleRate) const noexcept;
};
} // namespace ftp::host
