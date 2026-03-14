#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>

namespace ftp::dsp
{
struct OnsetEvent
{
    std::int32_t sampleOffset = 0;
    double audioTimeSec = 0.0;
    float peakAbsRaw = 0.0f;
};

class OnsetDetector
{
public:
    void prepare(double sampleRate);
    void reset();

    void setSensitivity(float sensitivity) noexcept;
    void setGainMultiplier(float gainMultiplier) noexcept;
    void setDetectEnabled(bool enabled) noexcept;
    void setBeatIntervalMs(double beatIntervalMs) noexcept;

    std::size_t process(const float* channelData,
                        std::int32_t numSamples,
                        std::int64_t blockStartSample,
                        OnsetEvent* outputEvents,
                        std::size_t maxEvents) noexcept;

private:
    struct HistoryFrame
    {
        std::int64_t sampleIndex = 0;
        float env = 0.0f;
    };

    double sampleRate_ = 44100.0;
    std::int64_t lastOnsetSample_ = std::numeric_limits<std::int64_t>::min() / 2;
    std::int64_t cooldownSamples_ = 512;
    float prevSample_ = 0.0f;
    float prevEnv_ = 0.0f;
    float noiseFloor_ = 0.01f;
    float sensitivity_ = 0.8f;
    float gainMultiplier_ = 1.0f + 0.8f * 4.0f;
    bool detectEnabled_ = false;
    std::deque<HistoryFrame> history_;
    static constexpr std::size_t historyLimit_ = 256;
};
} // namespace ftp::dsp
