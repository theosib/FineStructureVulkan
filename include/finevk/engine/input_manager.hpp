#pragma once

/**
 * @file input_manager.hpp
 * @brief Comprehensive input management with "fat" events and state tracking
 *
 * Design based on API_IMPROVEMENTS.md and finevox/docs/10-input.md.
 * Key feature: Every event includes COMPLETE input state at time of event,
 * allowing queries like "is W pressed?" during any event type.
 */

#include "finevk/window/window.hpp"

#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <queue>
#include <string>
#include <chrono>

namespace finevk {

// ============================================================================
// InputState - Complete snapshot of all input state
// ============================================================================

/**
 * @brief Complete snapshot of keyboard, mouse, and modifier state
 *
 * Copyable for simulation/replay purposes. All queries are O(1) or O(n)
 * where n is the number of pressed keys (typically small).
 */
struct InputState {
    // Keyboard state - set of currently pressed keys
    std::unordered_set<Key> pressedKeys;

    // Mouse button state - set of currently pressed buttons
    std::unordered_set<MouseButton> pressedButtons;

    // Mouse position and movement
    glm::vec2 mousePosition{0.0f};
    glm::vec2 mouseDelta{0.0f};  // Change since last frame

    // Scroll wheel delta this frame
    glm::vec2 scrollDelta{0.0f};

    // Modifier state (cached for convenience)
    Modifier modifiers = Modifier::None;

    // ========================================================================
    // Query methods
    // ========================================================================

    /// Check if a specific key is currently pressed
    [[nodiscard]] bool isKeyPressed(Key key) const {
        return pressedKeys.find(key) != pressedKeys.end();
    }

    /// Check if a specific mouse button is currently pressed
    [[nodiscard]] bool isMouseButtonPressed(MouseButton button) const {
        return pressedButtons.find(button) != pressedButtons.end();
    }

    /// Get all currently pressed keys
    [[nodiscard]] std::vector<Key> getPressedKeys() const {
        return {pressedKeys.begin(), pressedKeys.end()};
    }

    /// Get all currently pressed mouse buttons
    [[nodiscard]] std::vector<MouseButton> getPressedButtons() const {
        return {pressedButtons.begin(), pressedButtons.end()};
    }

    // Modifier convenience methods
    [[nodiscard]] bool isShiftPressed() const {
        return static_cast<uint32_t>(modifiers) & static_cast<uint32_t>(Modifier::Shift);
    }
    [[nodiscard]] bool isControlPressed() const {
        return static_cast<uint32_t>(modifiers) & static_cast<uint32_t>(Modifier::Control);
    }
    [[nodiscard]] bool isAltPressed() const {
        return static_cast<uint32_t>(modifiers) & static_cast<uint32_t>(Modifier::Alt);
    }
    [[nodiscard]] bool isSuperPressed() const {
        return static_cast<uint32_t>(modifiers) & static_cast<uint32_t>(Modifier::Super);
    }

    // Default copy/move
    InputState() = default;
    InputState(const InputState&) = default;
    InputState& operator=(const InputState&) = default;
    InputState(InputState&&) = default;
    InputState& operator=(InputState&&) = default;
};

// ============================================================================
// InputEvent - Fat event with complete state
// ============================================================================

/**
 * @brief Event types for input events
 */
enum class InputEventType {
    KeyPress,
    KeyRelease,
    KeyRepeat,
    MouseButtonPress,
    MouseButtonRelease,
    MouseMove,
    MouseScroll,
    CharInput  // For text input (UTF-32 codepoint)
};

/**
 * @brief Input event with event-specific data AND complete input state
 *
 * The "fat event" design allows querying any input state during any event.
 * For example, during a MouseMove event, you can check if W is pressed.
 */
struct InputEvent {
    InputEventType type;

    // Event-specific data (only relevant fields are set based on type)
    Key key = 0;                    // For Key* events
    MouseButton mouseButton = 0;    // For MouseButton* events
    uint32_t character = 0;         // For CharInput (UTF-32 codepoint)

    // COMPLETE input state at time of event
    InputState state;

    // Timestamp (seconds since InputManager creation)
    double time = 0.0;
};

// ============================================================================
// InputManager - Comprehensive input handling
// ============================================================================

/**
 * @brief Manages input state and provides fat events
 *
 * Usage:
 * ```cpp
 * auto input = InputManager::create(window);
 *
 * // Option 1: Event callback
 * input->setEventCallback([](const InputEvent& e) {
 *     if (e.type == InputEventType::KeyPress && e.key == GLFW_KEY_W) {
 *         // W pressed, can also check e.state.isShiftPressed() etc.
 *     }
 * });
 *
 * // Option 2: Poll events
 * InputEvent event;
 * while (input->pollEvent(event)) {
 *     // Handle event
 * }
 *
 * // Option 3: Direct state queries
 * if (input->isKeyPressed(GLFW_KEY_W)) { ... }
 * ```
 */
class InputManager {
public:
    using EventCallback = std::function<void(const InputEvent&)>;

    /// Create an InputManager bound to a window
    static std::unique_ptr<InputManager> create(Window* window);

    ~InputManager();

    // Non-copyable, non-movable (owns window callbacks)
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    InputManager(InputManager&&) = delete;
    InputManager& operator=(InputManager&&) = delete;

    // ========================================================================
    // Frame update
    // ========================================================================

    /**
     * @brief Call once per frame before processing input
     *
     * - Clears per-frame state (keysPressed, keysReleased, mouseDelta, scrollDelta)
     * - Dispatches queued events to callback (if set)
     */
    void update();

    // ========================================================================
    // Event handling
    // ========================================================================

    /// Set callback for all input events (receives fat events)
    void setEventCallback(EventCallback callback) { eventCallback_ = std::move(callback); }

    /// Poll next event from queue (returns false when empty)
    bool pollEvent(InputEvent& event);

    /// Clear all pending events
    void clearEvents();

    // ========================================================================
    // Direct state queries (current frame)
    // ========================================================================

    /// Get complete current input state
    [[nodiscard]] const InputState& currentState() const { return state_; }

    /// Check if key is currently held down
    [[nodiscard]] bool isKeyDown(Key key) const { return state_.isKeyPressed(key); }

    /// Check if key was pressed this frame (first frame only)
    [[nodiscard]] bool wasKeyPressed(Key key) const { return keysPressed_.find(key) != keysPressed_.end(); }

    /// Check if key was released this frame
    [[nodiscard]] bool wasKeyReleased(Key key) const { return keysReleased_.find(key) != keysReleased_.end(); }

    /// Check if mouse button is currently held down
    [[nodiscard]] bool isMouseButtonDown(MouseButton button) const {
        return state_.isMouseButtonPressed(button);
    }

    /// Check if mouse button was pressed this frame
    [[nodiscard]] bool wasMouseButtonPressed(MouseButton button) const {
        return buttonsPressed_.find(button) != buttonsPressed_.end();
    }

    /// Check if mouse button was released this frame
    [[nodiscard]] bool wasMouseButtonReleased(MouseButton button) const {
        return buttonsReleased_.find(button) != buttonsReleased_.end();
    }

    /// Get current mouse position
    [[nodiscard]] glm::vec2 mousePosition() const { return state_.mousePosition; }

    /// Get mouse movement since last frame
    [[nodiscard]] glm::vec2 mouseDelta() const { return state_.mouseDelta; }

    /// Get scroll wheel delta this frame
    [[nodiscard]] glm::vec2 scrollDelta() const { return state_.scrollDelta; }

    // ========================================================================
    // Mouse capture
    // ========================================================================

    /// Set mouse capture mode (hides cursor, locks to window)
    void setMouseCaptured(bool captured);

    /// Check if mouse is captured
    [[nodiscard]] bool isMouseCaptured() const { return mouseCaptured_; }

    // ========================================================================
    // Action mapping (rebindable controls)
    // ========================================================================

    /// Map a named action to a key
    void mapAction(const std::string& action, Key key);

    /// Map a named action to a mouse button
    void mapActionToMouse(const std::string& action, MouseButton button);

    /// Check if action is currently active (key/button held)
    [[nodiscard]] bool isActionActive(const std::string& action) const;

    /// Check if action was triggered this frame (key/button pressed)
    [[nodiscard]] bool wasActionTriggered(const std::string& action) const;

    /// Remove action mapping
    void unmapAction(const std::string& action);

    /// Clear all action mappings
    void clearActionMappings();

    // ========================================================================
    // Simulation/testing
    // ========================================================================

    /// Inject a synthetic event (for testing/replay)
    void injectEvent(const InputEvent& event);

private:
    explicit InputManager(Window* window);

    void setupCallbacks();
    void handleKeyEvent(Key key, Action action, Modifier mods);
    void handleMouseButtonEvent(MouseButton button, Action action, Modifier mods);
    void handleMouseMoveEvent(double x, double y);
    void handleScrollEvent(double xoffset, double yoffset);
    void handleCharEvent(uint32_t codepoint);

    void queueEvent(InputEvent event);
    double currentTime() const;

    Window* window_ = nullptr;

    // Current state
    InputState state_;
    bool mouseCaptured_ = false;

    // Per-frame tracking (cleared in update())
    std::unordered_set<Key> keysPressed_;      // Keys pressed THIS frame
    std::unordered_set<Key> keysReleased_;     // Keys released THIS frame
    std::unordered_set<MouseButton> buttonsPressed_;
    std::unordered_set<MouseButton> buttonsReleased_;
    glm::vec2 scrollAccumulator_{0.0f};  // Accumulated scroll before update()
    glm::vec2 lastMousePosition_{0.0f};

    // Event queue
    std::queue<InputEvent> eventQueue_;
    EventCallback eventCallback_;

    // Action mappings
    std::unordered_map<std::string, Key> keyActions_;
    std::unordered_map<std::string, MouseButton> mouseActions_;

    // Timing
    std::chrono::steady_clock::time_point startTime_;
};

} // namespace finevk
