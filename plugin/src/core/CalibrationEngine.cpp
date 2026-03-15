#include "CalibrationEngine.h"

#include <algorithm>
#include <cmath>

namespace ftp::core
{
CalibrationEngine::CalibrationEngine(std::size_t targetSampleCount)
    : targetCount_(targetSampleCount)
{
}

void CalibrationEngine::reset()
{
    samples_.clear();
    result_ = {};
}

bool CalibrationEngine::addSample(double rawErrorMs, double beatIntervalMs)
{
    if (beatIntervalMs <= 0.0)
        return false;

    // Ignore outliers that are likely wrong-beat matches.
    if (std::abs(rawErrorMs) > beatIntervalMs * 0.45)
        return false;

    samples_.push_back(rawErrorMs);
    return true;
}

bool CalibrationEngine::isComplete() const noexcept
{
    return samples_.size() >= targetCount_;
}

std::size_t CalibrationEngine::currentSampleCount() const noexcept
{
    return samples_.size();
}

std::size_t CalibrationEngine::targetSampleCount() const noexcept
{
    return targetCount_;
}

CalibrationEngine::Result CalibrationEngine::finalize()
{
    if (samples_.empty())
    {
        result_ = {};
        return result_;
    }

    const auto center = median(samples_);
    std::vector<double> deviations;
    deviations.reserve(samples_.size());
    for (const auto sample : samples_)
        deviations.push_back(std::abs(sample - center));

    const auto mad = median(deviations);

    result_.offsetMs = std::clamp(center, -150.0, 150.0);
    result_.jitterMs = std::clamp(mad * 2.0, 0.5, 25.0);
    result_.valid = true;

    samples_.clear();
    return result_;
}

CalibrationEngine::Result CalibrationEngine::current() const noexcept
{
    return result_;
}

double CalibrationEngine::median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());
    const auto mid = values.size() / 2;
    if ((values.size() % 2) == 0)
        return (values[mid - 1] + values[mid]) * 0.5;
    return values[mid];
}
} // namespace ftp::core
