#include "../src/core/ScoringEngine.h"

#include <cassert>

void runCalibrationEngineTests();

namespace
{
void testScoringImprovesOnTightHits()
{
    ftp::core::ScoringEngine scoring;
    scoring.resetSession();
    scoring.incrementBeatCount();
    const auto first = scoring.processHit(2.0);
    scoring.incrementBeatCount();
    const auto second = scoring.processHit(1.0);
    assert(second.points >= first.points);
}

void testPocketModeShift()
{
    ftp::core::ScoringEngine scoring;
    scoring.resetSession();
    scoring.setPocketMode(ftp::core::PocketMode::laidBack);
    scoring.incrementBeatCount();
    const auto centered = scoring.processHit(20.0);
    assert(centered.accuracy > 0.9);
}
} // namespace

int main()
{
    testScoringImprovesOnTightHits();
    testPocketModeShift();
    runCalibrationEngineTests();
    return 0;
}
