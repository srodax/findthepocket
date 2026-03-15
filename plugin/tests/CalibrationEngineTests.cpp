#include "../src/core/CalibrationEngine.h"

#include <cassert>

namespace
{
void testCalibrationFinalize()
{
    ftp::core::CalibrationEngine calibration(4);
    calibration.addSample(8.0, 500.0);
    calibration.addSample(10.0, 500.0);
    calibration.addSample(9.0, 500.0);
    calibration.addSample(11.0, 500.0);
    assert(calibration.isComplete());

    const auto result = calibration.finalize();
    assert(result.valid);
    assert(result.offsetMs >= 8.0 && result.offsetMs <= 11.0);
    assert(result.jitterMs >= 0.5 && result.jitterMs <= 25.0);
}
} // namespace

void runCalibrationEngineTests()
{
    testCalibrationFinalize();
}
