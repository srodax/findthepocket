#pragma once

#include <cstddef>
#include <deque>

namespace ftp::core
{
enum class PocketMode
{
    strict,
    laidBack,
    push
};

class ScoringEngine
{
public:
    struct HitScore
    {
        double correctedErrorMs = 0.0;
        double points = 0.0;
        double accuracy = 0.0;
        double groove = 0.5;
        double multiplier = 0.625;
        bool plottable = false;
    };

    struct SessionSummary
    {
        double totalScore = 0.0;
        std::size_t hitCount = 0;
        std::size_t beatCount = 0;
        double sessionAccuracy = 0.0;
        double sessionConsistency = 0.0;
        double sessionStability = 0.5;
        double sessionPocket = 0.625;
        double normalizedPercent = 0.0;
    };

    void resetSession();
    void setPocketMode(PocketMode mode) noexcept;
    void setCalibration(double offsetMs, double jitterMs) noexcept;
    void incrementBeatCount() noexcept;

    HitScore processHit(double rawSignedErrorMs);
    SessionSummary summary() const noexcept;

    static constexpr double tightMs = 5.0;
    static constexpr double borderlineMs = 10.0;
    static constexpr double looseMs = 20.0;

private:
    static double clamp(double value, double min, double max);
    static double targetForPocketMode(PocketMode mode) noexcept;

    PocketMode pocketMode_ = PocketMode::strict;
    double calibrationOffsetMs_ = 0.0;
    double calibrationJitterMs_ = 5.0;

    std::deque<double> recentErrors_;
    static constexpr std::size_t scoreWindowHits = 12;

    double totalScore_ = 0.0;
    std::size_t hitCount_ = 0;
    std::size_t beatCount_ = 0;
    double groove_ = 0.5;

    double sessionSumAccuracy_ = 0.0;
    double sessionSumConsistency_ = 0.0;
    double sessionSumStability_ = 0.5;
    double sessionSumPocket_ = 0.625;
};
} // namespace ftp::core
