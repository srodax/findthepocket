#pragma once

#include <cstdint>

namespace ftp::core
{
class SessionEngine
{
public:
    enum class State
    {
        idle,
        running,
        calibrating
    };

    struct Remaining
    {
        double timeLeftSec = 0.0;
        std::uint32_t hitsLeft = 0;
    };

    void setRunDurationSeconds(double seconds) noexcept;
    void setBeatIntervalMs(double beatIntervalMs) noexcept;

    void startRun(double nowSec) noexcept;
    void startCalibration() noexcept;
    void stop() noexcept;

    bool isRunning() const noexcept;
    bool isCalibrating() const noexcept;
    State state() const noexcept;

    Remaining remaining(double nowSec) const noexcept;
    bool shouldAutoStop(double nowSec) const noexcept;

private:
    State state_ = State::idle;
    double runDurationSec_ = 60.0;
    double beatIntervalMs_ = 600.0;
    double runEndSec_ = 0.0;
};
} // namespace ftp::core
