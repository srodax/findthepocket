#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ftp::dsp
{
void OnsetDetector::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 44100.0;
    setBeatIntervalMs(600.0);
    reset();
}

void OnsetDetector::reset()
{
    lastOnsetSample_ = std::numeric_limits<std::int64_t>::min() / 2;
    prevSample_ = 0.0f;
    prevEnv_ = 0.0f;
    noiseFloor_ = 0.01f;
    history_.clear();
}

void OnsetDetector::setSensitivity(float sensitivity) noexcept
{
    sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
}

void OnsetDetector::setGainMultiplier(float gainMultiplier) noexcept
{
    gainMultiplier_ = std::clamp(gainMultiplier, 0.5f, 8.0f);
}

void OnsetDetector::setDetectEnabled(bool enabled) noexcept
{
    detectEnabled_ = enabled;
}

void OnsetDetector::setBeatIntervalMs(double beatIntervalMs) noexcept
{
    const auto cooldownMs = std::clamp(beatIntervalMs * 0.35, 85.0, 190.0);
    cooldownSamples_ = std::max<std::int64_t>(8, static_cast<std::int64_t>((cooldownMs / 1000.0) * sampleRate_));
}

std::size_t OnsetDetector::process(const float* channelData,
                                   std::int32_t numSamples,
                                   std::int64_t blockStartSample,
                                   OnsetEvent* outputEvents,
                                   std::size_t maxEvents) noexcept
{
    if (channelData == nullptr || numSamples <= 0 || outputEvents == nullptr || maxEvents == 0)
        return 0;

    std::size_t eventCount = 0;
    for (std::int32_t i = 0; i < numSamples; ++i)
    {
        const auto sample = channelData[i];
        const auto absSample = std::abs(sample);

        if (detectEnabled_)
        {
            // Quantization-style transient extraction: pre-emphasis, envelope, novelty, adaptive gate.
            const auto preEmphasis = sample - prevSample_ * 0.97f;
            const auto boosted = std::abs(preEmphasis) * gainMultiplier_;
            const auto env = prevEnv_ * 0.55f + boosted * 0.45f;
            const auto novelty = std::max(0.0f, env - prevEnv_);

            noiseFloor_ = noiseFloor_ * 0.995f + env * 0.005f;
            const auto noveltyGate = std::max(0.004f, noiseFloor_ * (1.65f + (1.0f - sensitivity_) * 0.55f));
            const auto ampGate = std::max(0.01f, noiseFloor_ * 1.2f);

            const auto absoluteSampleIndex = blockStartSample + i;
            history_.push_back({ absoluteSampleIndex, env });
            while (history_.size() > historyLimit_)
                history_.pop_front();
            if (novelty > noveltyGate &&
                env > ampGate &&
                (absoluteSampleIndex - lastOnsetSample_) > cooldownSamples_)
            {
                const auto prevNovelty = std::max(1.0e-8f, prevEnv_ - noiseFloor_);
                const auto denom = novelty + prevNovelty;
                const auto frac = denom > 0.0f ? std::clamp(novelty / denom, 0.0f, 1.0f) : 0.0f;
                auto onsetSample = static_cast<double>(absoluteSampleIndex) - 1.0 + static_cast<double>(frac);

                // Backtrack to an earlier attack crossing to reduce detection latency bias.
                const auto onsetLevel = noiseFloor_ + std::max(0.006f, (env - noiseFloor_) * 0.12f);
                if (history_.size() >= 2)
                {
                    for (std::size_t j = history_.size() - 1; j > 0; --j)
                    {
                        const auto& prevFrame = history_[j - 1];
                        const auto& curFrame = history_[j];
                        if (curFrame.sampleIndex > absoluteSampleIndex)
                            continue;
                        if (prevFrame.sampleIndex < absoluteSampleIndex - static_cast<std::int64_t>(historyLimit_))
                            break;
                        if (prevFrame.env <= onsetLevel && curFrame.env >= onsetLevel)
                        {
                            const auto d = curFrame.env - prevFrame.env;
                            const auto backtrackFrac = d > 1.0e-8f
                                ? std::clamp((onsetLevel - prevFrame.env) / d, 0.0f, 1.0f)
                                : 0.0f;
                            onsetSample = static_cast<double>(prevFrame.sampleIndex) + static_cast<double>(backtrackFrac);
                            break;
                        }
                    }
                }

                if (eventCount < maxEvents)
                {
                    auto& e = outputEvents[eventCount++];
                    e.sampleOffset = i;
                    e.audioTimeSec = onsetSample / sampleRate_;
                    e.peakAbsRaw = absSample;
                }

                lastOnsetSample_ = absoluteSampleIndex;
            }

            prevEnv_ = env;
            prevSample_ = sample;
        }
        else
        {
            prevEnv_ = prevEnv_ * 0.98f + absSample * 0.02f;
            prevSample_ = sample;
        }
    }

    return eventCount;
}
} // namespace ftp::dsp
