#include "finevk/engine/game_loop.hpp"
#include "finevk/core/logging.hpp"
#include <chrono>

namespace finevk {

GameLoop::GameLoop(Window* window, RenderTarget* renderTarget, DeferredDisposer* disposer)
    : window_(window)
    , renderTarget_(renderTarget)
    , disposer_(disposer) {
    if (!window_) {
        throw std::runtime_error("GameLoop: window cannot be null");
    }
    if (!renderTarget_) {
        throw std::runtime_error("GameLoop: renderTarget cannot be null");
    }
    if (!disposer_) {
        throw std::runtime_error("GameLoop: disposer cannot be null");
    }
}

GameLoop::~GameLoop() {
    if (!isShutdown_) {
        shutdown();
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

void GameLoop::setup() {
    if (isSetup_) {
        return;  // Already set up
    }

    // Register window resize callback
    // Note: We use a lambda that captures 'this' to call our member function
    // This is safe because GameLoop is non-movable and we unregister in shutdown()
    window_->onResize([this](uint32_t w, uint32_t h) {
        this->handleResize(w, h);
    });

    clock_.start();
    isSetup_ = true;
    isShutdown_ = false;  // Allow restart after shutdown

    FINEVK_DEBUG(LogCategory::Core, "GameLoop setup complete");
}

void GameLoop::run() {
    if (isRunning_) {
        FINEVK_WARN(LogCategory::Core, "GameLoop::run() called while already running");
        return;
    }

    if (!isSetup_) {
        setup();
    }

    isRunning_ = true;
    shouldQuit_ = false;
    frameNumber_ = 0;

    FINEVK_INFO(LogCategory::Core, "GameLoop started");

    while (!shouldQuit()) {
        try {
            float dt = clock_.tick();
            runFrame(dt);
            frameNumber_++;
        } catch (const std::exception& e) {
            if (!onError(e)) {
                break;  // Exit loop on fatal error
            }
        }
    }

    isRunning_ = false;
    FINEVK_INFO(LogCategory::Core, "GameLoop exited");
}

void GameLoop::shutdown() {
    if (isShutdown_) {
        return;  // Already shut down
    }

    // Unregister window callbacks
    if (window_) {
        window_->onResize(nullptr);
    }

    isSetup_ = false;  // Allow re-setup
    isShutdown_ = true;
    FINEVK_DEBUG(LogCategory::Core, "GameLoop shutdown complete");
}

// =============================================================================
// Frame Execution
// =============================================================================

void GameLoop::runFrame(float dt) {
    // Process events
    try {
        onProcessEvents();
    } catch (const std::exception& e) {
        if (!onError(e)) {
            quit();
            return;
        }
    }

    // Fixed timestep updates
    accumulator_ += dt;
    int updateCount = 0;

    while (accumulator_ >= fixedTimestep_) {
        try {
            onFixedUpdate(fixedTimestep_);
        } catch (const std::exception& e) {
            if (!onError(e)) {
                quit();
                return;
            }
        }

        accumulator_ -= fixedTimestep_;
        updateCount++;

        // Prevent spiral of death
        if (updateCount >= maxUpdatesPerFrame_) {
            accumulator_ = 0.0f;
            FINEVK_WARN(LogCategory::Core,
                "GameLoop: Falling behind, skipping updates");
            break;
        }
    }

    // Variable rate update
    try {
        onUpdate(dt);
    } catch (const std::exception& e) {
        if (!onError(e)) {
            quit();
            return;
        }
    }

    // Render with interpolation
    float interpolation = accumulator_ / fixedTimestep_;
    try {
        onRender(dt, interpolation);
    } catch (const std::exception& e) {
        if (!onError(e)) {
            quit();
            return;
        }
    }

    // Frame end
    try {
        onFrameEnd();
    } catch (const std::exception& e) {
        if (!onError(e)) {
            quit();
            return;
        }
    }

    // Garbage collection
    if (gcInterval_ > 0 && ++gcCounter_ >= gcInterval_) {
        gcCounter_ = 0;
        try {
            onGarbageCollect();
        } catch (const std::exception& e) {
            if (!onError(e)) {
                quit();
                return;
            }
        }
    }

    // Frame pacing
    if (targetFrameTime_ > 0.0f) {
        float sleepTime = onComputeSleep(targetFrameTime_, dt);
        if (sleepTime > 0.0f) {
            FrameClock::sleep(sleepTime);
        }
    }
}

// =============================================================================
// Virtual Method Implementations (Default Behavior)
// =============================================================================

void GameLoop::onProcessEvents() {
    if (window_) {
        window_->pollEvents();
    }
    if (eventsListener_) {
        eventsListener_();
    }
}

void GameLoop::onFixedUpdate(float fixedDt) {
    if (fixedUpdateListener_) {
        fixedUpdateListener_(fixedDt);
    }
}

void GameLoop::onUpdate(float dt) {
    if (updateListener_) {
        updateListener_(dt);
    }
}

void GameLoop::onRender(float dt, float interpolation) {
    if (renderListener_) {
        renderListener_(dt, interpolation);
    }
}

void GameLoop::onFrameEnd() {
    if (frameEndListener_) {
        frameEndListener_();
    }
}

void GameLoop::onWindowResize(uint32_t width, uint32_t height) {
    if (resizeListener_) {
        resizeListener_(width, height);
    }
}

bool GameLoop::onError(const std::exception& e) {
    // Default: Log and continue
    FINEVK_ERROR(LogCategory::Core,
        "GameLoop error: " + std::string(e.what()));

    if (errorListener_) {
        return errorListener_(e);
    }

    return true;  // Continue by default
}

void GameLoop::onGarbageCollect() {
    // Mark frame passed for deferred disposals
    if (disposer_) {
        disposer_->processFrame();
    }

    // Call user listener if set
    if (gcListener_) {
        gcListener_();
    }

    // Dispose resources within time budget
    if (disposer_ && gcTimeBudget_ > 0.0f) {
        auto startTime = std::chrono::high_resolution_clock::now();
        auto budgetDuration = std::chrono::duration<float>(gcTimeBudget_);

        while (true) {
            auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
            if (elapsed >= budgetDuration) {
                break;  // Time budget exhausted
            }

            if (!disposer_->tryDisposeOne()) {
                break;  // No more ready resources
            }
        }
    }
}

float GameLoop::onComputeSleep(float targetFrameTime, float elapsed) {
    return targetFrameTime - elapsed;
}

bool GameLoop::shouldQuit() const {
    return shouldQuit_ || (window_ && !window_->isOpen());
}

// =============================================================================
// Private Helpers
// =============================================================================

void GameLoop::handleResize(uint32_t width, uint32_t height) {
    // RenderTarget will automatically handle swap chain recreation
    // when it detects the window resize
    try {
        onWindowResize(width, height);
    } catch (const std::exception& e) {
        onError(e);
    }
}

} // namespace finevk
