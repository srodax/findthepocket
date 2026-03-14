#include "SessionEngine.h"

#include <algorithm>
#include <cmath>

namespace ftp::core
{
void SessionEngine::setRunDurationSeconds(double seconds) noexcept
{
    if (seconds > 0.0)
        runDurationSec_ = seconds;
}

void SessionEngine::setBeatIntervalMs(double beatIntervalMs) noexcept
{
    if (beatIntervalMs > 1.0)
        beatIntervalMs_ = beatIntervalMs;
}

void SessionEngine::startRun(double nowSec) noexcept
{
    state_ = State::running;
    runEndSec_ = nowSec + runDurationSec_;
}

void SessionEngine::startCalibration() noexcept
{
    state_ = State::calibrating;
    runEndSec_ = 0.0;
}

void SessionEngine::stop() noexcept
{
    state_ = State::idle;
    runEndSec_ = 0.0;
}

bool SessionEngine::isRunning() const noexcept
{
    return state_ == State::running;
}

bool SessionEngine::isCalibrating() const noexcept
{
    return state_ == State::calibrating;
}

SessionEngine::State SessionEngine::state() const noexcept
{
    return state_;
}

SessionEngine::Remaining SessionEngine::remaining(double nowSec) const noexcept
{
    Remaining out;
    if (state_ != State::running || runEndSec_ <= 0.0)
        return out;

    out.timeLeftSec = std::max(0.0, runEndSec_ - nowSec);
    const auto beatIntervalSec = beatIntervalMs_ / 1000.0;
    if (beatIntervalSec > 0.0)
        out.hitsLeft = static_cast<std::uint32_t>(std::ceil(out.timeLeftSec / beatIntervalSec));
    return out;
}

bool SessionEngine::shouldAutoStop(double nowSec) const noexcept
{
    return state_ == State::running && runEndSec_ > 0.0 && nowSec >= runEndSec_;
}
} // namespace ftp::core
