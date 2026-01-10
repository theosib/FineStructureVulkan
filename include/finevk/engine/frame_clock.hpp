#pragma once

#include <chrono>

namespace finevk {

/**
 * @brief Utility for measuring frame timing and FPS
 *
 * FrameClock provides high-resolution timing for frame delta calculations
 * and FPS tracking. It uses std::chrono for platform-independent timing.
 */
class FrameClock {
public:
    FrameClock();

    /// Start/restart the clock
    void start();

    /// Mark the end of a frame and return delta time in seconds
    float tick();

    /// Get the last frame's delta time in seconds
    float deltaTime() const { return deltaTime_; }

    /// Get the current FPS (frames per second)
    float fps() const { return fps_; }

    /// Get total elapsed time since start() in seconds
    float totalTime() const;

    /// Sleep for a specified duration in seconds
    static void sleep(float seconds);

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    TimePoint startTime_;
    TimePoint lastFrameTime_;
    float deltaTime_ = 0.0f;
    float fps_ = 0.0f;

    // FPS smoothing
    static constexpr int FPS_SAMPLE_COUNT = 60;
    float fpsAccumulator_ = 0.0f;
    int fpsFrameCount_ = 0;
};

} // namespace finevk
