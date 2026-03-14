#pragma once

#include <cstddef>
#include <vector>

namespace ftp::core
{
class CalibrationEngine
{
public:
    struct Result
    {
        double offsetMs = 0.0;
        double jitterMs = 5.0;
        bool valid = false;
    };

    explicit CalibrationEngine(std::size_t targetSampleCount = 24);

    void reset();
    bool addSample(double rawErrorMs, double beatIntervalMs);
    bool isComplete() const noexcept;
    std::size_t currentSampleCount() const noexcept;
    std::size_t targetSampleCount() const noexcept;

    Result finalize();
    Result current() const noexcept;

private:
    static double median(std::vector<double> values);

    std::size_t targetCount_ = 24;
    std::vector<double> samples_;
    Result result_;
};
} // namespace ftp::core
