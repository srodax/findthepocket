#include "TransportSync.h"

#include <algorithm>
#include <cmath>

namespace ftp::host
{
HostTiming TransportSync::read(const juce::AudioPlayHead* playHead) const
{
    HostTiming out;
    if (playHead == nullptr)
        return out;

    const auto posOptional = playHead->getPosition();
    if (!posOptional.hasValue())
        return out;
    const auto& pos = *posOptional;

    const auto bpm = pos.getBpm().orFallback(120.0);
    const auto ppq = pos.getPpqPosition().orFallback(0.0);
    const auto projectSec = pos.getTimeInSeconds().orFallback(0.0);
    const auto sig = pos.getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature{4, 4});
    const auto samplePos = pos.getTimeInSamples().orFallback(0);

    out.valid = true;
    out.isPlaying = pos.getIsPlaying();
    out.isRecording = pos.getIsRecording();
    out.bpm = bpm;
    out.ppqStart = ppq;
    out.projectTimeStartSec = projectSec;
    out.numerator = sig.numerator;
    out.denominator = sig.denominator;
    out.timeInSamples = samplePos;
    return out;
}

GridMatch TransportSync::matchToGrid(const HostTiming& timing,
                                     double eventAudioTimeSec,
                                     double sampleRate,
                                     double gridDivision) const noexcept
{
    GridMatch out;
    if (!timing.valid || timing.bpm <= 0.0 || gridDivision <= 0.0 || sampleRate <= 0.0)
        return out;

    // eventAudioTimeSec is absolute stream time in seconds, so map to PPQ using current tempo.
    const auto eventSecFromBlockStart = eventAudioTimeSec - (static_cast<double>(timing.timeInSamples) / sampleRate);
    const auto ppqDelta = eventSecFromBlockStart * (timing.bpm / 60.0);
    const auto ppqAtEvent = timing.ppqStart + ppqDelta;
    const auto projectTimeSec = timing.projectTimeStartSec + eventSecFromBlockStart;

    const auto scaled = ppqAtEvent * gridDivision;
    const auto nearest = std::round(scaled) / gridDivision;
    const auto ppqError = ppqAtEvent - nearest;
    const auto signedErrorMs = ppqError * (60.0 / timing.bpm) * 1000.0;

    out.valid = true;
    out.ppqAtEvent = ppqAtEvent;
    out.nearestGridPpq = nearest;
    out.projectTimeSec = projectTimeSec;
    out.signedErrorMs = signedErrorMs;
    return out;
}

std::int32_t TransportSync::countBeatsCrossed(const HostTiming& timing, int numSamples, double sampleRate) const noexcept
{
    if (!timing.valid || timing.bpm <= 0.0 || numSamples <= 0 || sampleRate <= 0.0)
        return 0;

    const auto ppqAdvance = (static_cast<double>(numSamples) / sampleRate) * (timing.bpm / 60.0);
    const auto startBeat = static_cast<std::int64_t>(std::floor(timing.ppqStart));
    const auto endBeat = static_cast<std::int64_t>(std::floor(timing.ppqStart + ppqAdvance));
    return static_cast<std::int32_t>(std::max<std::int64_t>(0, endBeat - startBeat));
}
} // namespace ftp::host
