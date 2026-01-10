#include "finevk/engine/frame_clock.hpp"
#include <thread>

namespace finevk {

FrameClock::FrameClock() {
    start();
}

void FrameClock::start() {
    startTime_ = Clock::now();
    lastFrameTime_ = startTime_;
    deltaTime_ = 0.0f;
    fps_ = 0.0f;
    fpsAccumulator_ = 0.0f;
    fpsFrameCount_ = 0;
}

float FrameClock::tick() {
    TimePoint currentTime = Clock::now();
    std::chrono::duration<float> elapsed = currentTime - lastFrameTime_;
    deltaTime_ = elapsed.count();
    lastFrameTime_ = currentTime;

    // Update FPS with smoothing
    fpsAccumulator_ += deltaTime_;
    fpsFrameCount_++;

    if (fpsFrameCount_ >= FPS_SAMPLE_COUNT) {
        fps_ = static_cast<float>(fpsFrameCount_) / fpsAccumulator_;
        fpsAccumulator_ = 0.0f;
        fpsFrameCount_ = 0;
    }

    return deltaTime_;
}

float FrameClock::totalTime() const {
    TimePoint currentTime = Clock::now();
    std::chrono::duration<float> elapsed = currentTime - startTime_;
    return elapsed.count();
}

void FrameClock::sleep(float seconds) {
    if (seconds > 0.0f) {
        std::this_thread::sleep_for(
            std::chrono::duration<float>(seconds));
    }
}

} // namespace finevk
