#pragma once

#include "finevk/engine/frame_clock.hpp"
#include "finevk/engine/deferred_disposer.hpp"
#include "finevk/window/window.hpp"
#include "finevk/rendering/render_target.hpp"
#include <functional>
#include <exception>

namespace finevk {

/**
 * @brief Game loop with fixed timestep logic and variable framerate rendering
 *
 * GameLoop provides a complete game loop implementation with:
 * - Fixed timestep for deterministic game logic
 * - Variable framerate rendering with interpolation
 * - Comprehensive interception points via virtual methods
 * - Optional listener callbacks for simple use cases
 * - Automatic window resize handling
 * - Exception-based error handling
 *
 * Usage patterns:
 * 1. Inheritance (preferred): Derive and override virtual methods
 * 2. Listeners: Use as-is and set callback functions
 *
 * Example (inheritance):
 * @code
 * class MyGame : public GameLoop {
 * public:
 *     MyGame(Window* window, RenderTarget* target)
 *         : GameLoop(window, target) {}
 *
 * protected:
 *     void onFixedUpdate(float dt) override {
 *         // Game logic at fixed rate
 *     }
 *
 *     void onRender(float dt, float interpolation) override {
 *         // Rendering at vsync rate
 *     }
 * };
 *
 * MyGame game(window, renderTarget);
 * game.setFixedTimestep(1.0f / 60.0f);
 * game.run();  // Blocks until exit
 * @endcode
 *
 * Example (listeners):
 * @code
 * GameLoop loop(window, renderTarget);
 * loop.setFixedUpdateListener([](float dt) {
 *     // Game logic
 * });
 * loop.setRenderListener([](float dt, float alpha) {
 *     // Rendering
 * });
 * loop.run();
 * @endcode
 */
class GameLoop {
public:
    // Listener function types
    using FixedUpdateListener = std::function<void(float fixedDt)>;
    using UpdateListener = std::function<void(float dt)>;
    using RenderListener = std::function<void(float dt, float interpolation)>;
    using EventsListener = std::function<void()>;
    using FrameEndListener = std::function<void()>;
    using ResizeListener = std::function<void(uint32_t width, uint32_t height)>;
    using ErrorListener = std::function<bool(const std::exception&)>;
    using GarbageCollectListener = std::function<void()>;

    /**
     * @brief Construct a game loop
     *
     * @param window The window to render to (non-owning reference)
     * @param renderTarget The render target (non-owning reference)
     * @param disposer The deferred disposer (default: global singleton)
     */
    GameLoop(Window* window, RenderTarget* renderTarget,
             DeferredDisposer* disposer = &DeferredDisposer::global());
    GameLoop(Window& window, RenderTarget& renderTarget,
             DeferredDisposer* disposer = &DeferredDisposer::global())
        : GameLoop(&window, &renderTarget, disposer) {}

    /// Virtual destructor - calls shutdown() if not already called
    virtual ~GameLoop();

    // Non-copyable, non-movable (owns state tied to window callbacks)
    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;
    GameLoop(GameLoop&&) = delete;
    GameLoop& operator=(GameLoop&&) = delete;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set fixed timestep for game logic in seconds (default: 1/60)
    void setFixedTimestep(float seconds) { fixedTimestep_ = seconds; }

    /// Set maximum updates per frame to prevent spiral of death (default: 5)
    void setMaxUpdatesPerFrame(int count) { maxUpdatesPerFrame_ = count; }

    /// Set garbage collection interval in frames (default: 60)
    void setGarbageCollectInterval(int frames) { gcInterval_ = frames; }

    /// Set garbage collection time budget in seconds (default: 0.001 = 1ms)
    void setGarbageCollectTimeBudget(float seconds) { gcTimeBudget_ = seconds; }

    /// Set target framerate (0 = unlimited, use vsync) (default: 0)
    void setTargetFramerate(float fps) {
        targetFrameTime_ = (fps > 0.0f) ? (1.0f / fps) : 0.0f;
    }

    // =========================================================================
    // Listener Setters
    // =========================================================================

    void setFixedUpdateListener(FixedUpdateListener listener) {
        fixedUpdateListener_ = listener;
    }

    void setUpdateListener(UpdateListener listener) {
        updateListener_ = listener;
    }

    void setRenderListener(RenderListener listener) {
        renderListener_ = listener;
    }

    void setEventsListener(EventsListener listener) {
        eventsListener_ = listener;
    }

    void setFrameEndListener(FrameEndListener listener) {
        frameEndListener_ = listener;
    }

    void setResizeListener(ResizeListener listener) {
        resizeListener_ = listener;
    }

    void setErrorListener(ErrorListener listener) {
        errorListener_ = listener;
    }

    void setGarbageCollectListener(GarbageCollectListener listener) {
        gcListener_ = listener;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Initialize resources
     *
     * Called automatically by run() if not done explicitly.
     * Can be called manually to separate setup from blocking run().
     */
    virtual void setup();

    /**
     * @brief Start the game loop (blocks until exit)
     *
     * Automatically calls setup() if not already done.
     * Runs the main game loop until shouldQuit() returns true.
     */
    void run();

    /**
     * @brief Clean up resources
     *
     * Called automatically by destructor if not done explicitly.
     */
    virtual void shutdown();

    /**
     * @brief Request the game loop to exit
     *
     * Loop will exit after current frame completes.
     */
    void quit() { shouldQuit_ = true; }

    // =========================================================================
    // State Queries
    // =========================================================================

    /// Check if setup has been called
    bool isSetup() const { return isSetup_; }

    /// Check if loop is currently running
    bool isRunning() const { return isRunning_; }

    /// Check if shutdown has been called
    bool isShutdown() const { return isShutdown_; }

    /// Get the frame clock
    const FrameClock& clock() const { return clock_; }

    /// Get current frame number
    uint64_t frameNumber() const { return frameNumber_; }

    // =========================================================================
    // Virtual Methods (Override Points)
    // =========================================================================

protected:
    /**
     * @brief Process window events
     *
     * Default: Calls window->pollEvents() and eventsListener_ if set.
     */
    virtual void onProcessEvents();

    /**
     * @brief Fixed timestep update for game logic
     *
     * Called at fixed rate (e.g., 60 Hz). Use for physics, AI, game rules.
     * Default: Calls fixedUpdateListener_ if set.
     *
     * @param fixedDt Fixed delta time in seconds
     */
    virtual void onFixedUpdate(float fixedDt);

    /**
     * @brief Variable rate update
     *
     * Called once per frame at actual framerate. Use for frame-dependent logic
     * like user input processing or animations that should be frame-rate smooth.
     * Default: Calls updateListener_ if set.
     *
     * @param dt Delta time since last frame in seconds
     */
    virtual void onUpdate(float dt);

    /**
     * @brief Render the frame
     *
     * Default: Calls renderListener_ if set.
     *
     * @param dt Delta time since last frame in seconds
     * @param interpolation Value in [0, 1] for interpolating between game states
     */
    virtual void onRender(float dt, float interpolation);

    /**
     * @brief Called after frame presentation, before timing sleep
     *
     * Opportunity for frame-end bookkeeping.
     * Default: Calls frameEndListener_ if set.
     */
    virtual void onFrameEnd();

    /**
     * @brief Handle window resize
     *
     * Called after automatic resize handling (RenderTarget already updated).
     * Default: Calls resizeListener_ if set.
     *
     * @param width New window width
     * @param height New window height
     */
    virtual void onWindowResize(uint32_t width, uint32_t height);

    /**
     * @brief Handle errors during game loop
     *
     * Default: Logs error and returns true (continue running).
     * Call errorListener_ if set.
     *
     * @param e The exception that was caught
     * @return true to continue running, false to exit loop
     */
    virtual bool onError(const std::exception& e);

    /**
     * @brief Periodic garbage collection opportunity
     *
     * Called every gcInterval_ frames.
     * Default implementation:
     * 1. Calls disposer_->processFrame() to decrement frame counters
     * 2. Calls gcListener_ if set
     * 3. Calls disposer_->tryDisposeOne() in loop until time budget exhausted
     *
     * Override to customize GC behavior or add custom cleanup.
     */
    virtual void onGarbageCollect();

    /**
     * @brief Compute sleep time for frame pacing
     *
     * Override to implement custom timing (e.g., VR 90Hz, tool mode).
     * Default: Returns targetFrameTime_ - elapsed.
     *
     * @param targetFrameTime Target frame duration in seconds
     * @param elapsed Time elapsed this frame in seconds
     * @return Sleep time in seconds (0 for no sleep)
     */
    virtual float onComputeSleep(float targetFrameTime, float elapsed);

    /**
     * @brief Check if loop should quit
     *
     * Override to add custom quit conditions (e.g., check escape key).
     * Default: Returns shouldQuit_ || window->shouldClose().
     *
     * @return true if loop should exit
     */
    virtual bool shouldQuit() const;

    // =========================================================================
    // Protected Accessors
    // =========================================================================

    Window* window() { return window_; }
    RenderTarget* renderTarget() { return renderTarget_; }

private:
    // Window resize callback (registered with Window)
    void handleResize(uint32_t width, uint32_t height);

    // Run one frame of the game loop
    void runFrame(float dt);

    // Non-owning references
    Window* window_ = nullptr;
    RenderTarget* renderTarget_ = nullptr;
    DeferredDisposer* disposer_ = nullptr;

    // Timing
    FrameClock clock_;
    float fixedTimestep_ = 1.0f / 60.0f;
    float targetFrameTime_ = 0.0f;  // 0 = unlimited
    float accumulator_ = 0.0f;
    int maxUpdatesPerFrame_ = 5;

    // State
    bool isSetup_ = false;
    bool isRunning_ = false;
    bool isShutdown_ = false;
    bool shouldQuit_ = false;
    uint64_t frameNumber_ = 0;

    // Garbage collection
    int gcInterval_ = 60;
    int gcCounter_ = 0;
    float gcTimeBudget_ = 0.001f;  // 1ms by default

    // Listeners (optional, used by default virtual implementations)
    FixedUpdateListener fixedUpdateListener_;
    UpdateListener updateListener_;
    RenderListener renderListener_;
    EventsListener eventsListener_;
    FrameEndListener frameEndListener_;
    ResizeListener resizeListener_;
    ErrorListener errorListener_;
    GarbageCollectListener gcListener_;
};

} // namespace finevk
