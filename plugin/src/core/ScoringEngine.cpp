#include "ScoringEngine.h"

#include <algorithm>
#include <cmath>

namespace ftp::core
{
void ScoringEngine::resetSession()
{
    recentErrors_.clear();
    totalScore_ = 0.0;
    hitCount_ = 0;
    beatCount_ = 0;
    groove_ = 0.5;
    sessionSumAccuracy_ = 0.0;
    sessionSumConsistency_ = 0.0;
    sessionSumStability_ = 0.5;
    sessionSumPocket_ = 0.625;
}

void ScoringEngine::setPocketMode(PocketMode mode) noexcept
{
    pocketMode_ = mode;
}

void ScoringEngine::setCalibration(double offsetMs, double jitterMs) noexcept
{
    calibrationOffsetMs_ = offsetMs;
    calibrationJitterMs_ = clamp(jitterMs, 5.0, 25.0);
}

void ScoringEngine::incrementBeatCount() noexcept
{
    ++beatCount_;
}

ScoringEngine::HitScore ScoringEngine::processHit(double rawSignedErrorMs)
{
    HitScore out;

    const auto pocketTargetMs = targetForPocketMode(pocketMode_);
    const auto compensated = (rawSignedErrorMs - pocketTargetMs) - calibrationOffsetMs_;
    const auto absCompensated = std::abs(compensated);
    const auto correctedSignedError = absCompensated <= calibrationJitterMs_
        ? 0.0
        : std::copysign(absCompensated - calibrationJitterMs_, compensated);
    out.correctedErrorMs = correctedSignedError;

    recentErrors_.push_back(correctedSignedError);
    while (recentErrors_.size() > scoreWindowHits)
        recentErrors_.pop_front();

    const auto absCurrentError = std::abs(correctedSignedError);
    const auto normalized = absCurrentError / looseMs;
    const auto accuracy = clamp(1.0 - normalized * normalized, 0.0, 1.0);

    if (absCurrentError < tightMs)
        groove_ = clamp(groove_ + 0.08, 0.0, 1.0);
    else if (absCurrentError < borderlineMs)
        groove_ = clamp(groove_ + 0.02, 0.0, 1.0);
    else if (absCurrentError < looseMs)
        groove_ = clamp(groove_ - 0.05, 0.0, 1.0);
    else
        groove_ = clamp(groove_ - 0.12, 0.0, 1.0);

    const auto multiplier = 0.5 + 0.5 * groove_ * groove_;
    const auto noteScore = accuracy * multiplier;

    totalScore_ += noteScore;
    ++hitCount_;
    sessionSumAccuracy_ += accuracy;
    sessionSumConsistency_ = hitCount_ > 0 ? totalScore_ / static_cast<double>(hitCount_) : 0.0;
    sessionSumStability_ = groove_;
    sessionSumPocket_ = multiplier;

    out.points = noteScore;
    out.accuracy = accuracy;
    out.groove = groove_;
    out.multiplier = multiplier;
    out.plottable = std::abs(correctedSignedError) <= 30.0;
    return out;
}

ScoringEngine::SessionSummary ScoringEngine::summary() const noexcept
{
    SessionSummary out;
    out.totalScore = totalScore_;
    out.hitCount = hitCount_;
    out.beatCount = beatCount_;
    out.sessionAccuracy = sessionSumAccuracy_;
    out.sessionConsistency = sessionSumConsistency_;
    out.sessionStability = sessionSumStability_;
    out.sessionPocket = sessionSumPocket_;
    out.normalizedPercent = beatCount_ > 0 ? (totalScore_ / static_cast<double>(beatCount_)) * 100.0 : 0.0;
    return out;
}

double ScoringEngine::clamp(double value, double min, double max)
{
    return std::max(min, std::min(max, value));
}

double ScoringEngine::targetForPocketMode(PocketMode mode) noexcept
{
    switch (mode)
    {
        case PocketMode::laidBack:
            return 20.0;
        case PocketMode::push:
            return -20.0;
        case PocketMode::strict:
        default:
            return 0.0;
    }
}
} // namespace ftp::core
