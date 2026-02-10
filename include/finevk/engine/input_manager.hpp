#pragma once

/**
 * @file input_manager.hpp
 * @brief Comprehensive input management with priority-ordered listener chain
 *
 * Key features:
 * - "Fat events": Every event includes COMPLETE input state (keys held, mouse pos, etc.)
 * - Priority-ordered listeners with Reject/Used/Consumed propagation control
 * - Two-axis scroll accumulation for discrete ticks (hotbar, lists)
 * - Cursor mode management (normal, hidden, disabled for camera control)
 * - Action mapping for rebindable controls
 */

#include "finevk/window/window.hpp"
#include "finevk/engine/scroll_accumulator.hpp"

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
// Cursor Mode
// ============================================================================

/**
 * @brief Cursor visibility and positioning mode
 */
enum class CursorMode {
    Normal,    ///< Visible cursor, absolute screen coordinates
    Hidden,    ///< Invisible cursor but absolute positioning still works (tooltips)
    Disabled   ///< Locked/hidden cursor for camera control, use mouseDelta
};

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

    // Scroll wheel delta this frame (raw fractional values)
    glm::vec2 scrollDelta{0.0f};

    // Scroll accumulators for discrete ticks (hotbar, lists)
    ScrollAccumulator scrollAccumulatorX;
    ScrollAccumulator scrollAccumulatorY;

    // Cursor mode
    CursorMode cursorMode = CursorMode::Normal;

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
    Key key = 0;                    // For Key* events (GLFW keycode)
    int scancode = 0;               // For Key* events (physical key position, platform-specific)
    MouseButton mouseButton = 0;    // For MouseButton* events
    uint32_t character = 0;         // For CharInput (UTF-32 codepoint)

    // COMPLETE input state at time of event
    InputState state;

    // Timestamp (seconds since InputManager creation)
    double time = 0.0;
};

// ============================================================================
// Listener Chain
// ============================================================================

/**
 * @brief Result returned by input listeners to control event propagation
 */
enum class ListenerResult {
    Reject,    ///< Not for me, pass to next listener
    Used,      ///< I noted it, but others should see it too
    Consumed   ///< Mine exclusively, stop propagation
};

/**
 * @brief Input listener function type
 *
 * Receives events in priority order. Return:
 * - Reject: Listener doesn't want this event, continue to next
 * - Used: Listener acted on event but doesn't claim exclusivity, continue
 * - Consumed: Listener claims event exclusively, stop propagation
 */
using InputListener = std::function<ListenerResult(const InputEvent&)>;

/**
 * @brief Standard priority values for listener registration
 *
 * Lower numbers = higher precedence (executed first). Use these constants
 * for consistency, or choose custom values between them.
 *
 * Example usage:
 * @code
 * // GUI text input (highest priority, consumes all keys when focused)
 * input->addListener(guiTextListener, InputPriority::TextInput);
 *
 * // Open menu/inventory (high priority, blocks most game input)
 * input->addListener(menuListener, InputPriority::Menu);
 *
 * // HUD elements (consumes clicks on widgets only)
 * input->addListener(hudListener, InputPriority::HUD);
 *
 * // Game controls (WASD, mouse look)
 * input->addListener(gameListener, InputPriority::Game);
 *
 * // Debug overlay (lowest priority, catch-all for unhandled input)
 * input->addListener(debugListener, InputPriority::Debug);
 * @endcode
 */
namespace InputPriority {
    constexpr int TextInput = 100;   ///< Text fields consume all keys when focused
    constexpr int Menu = 200;        ///< Open menus/inventories block most input
    constexpr int HUD = 300;         ///< HUD elements (hotbar, health bars)
    constexpr int Game = 500;        ///< Game controls (WASD, mouse look, actions)
    constexpr int Debug = 900;       ///< Debug overlays, logging, catch-all
}

// ============================================================================
// InputManager - Comprehensive input handling
// ============================================================================

/**
 * @brief Manages input state with priority-ordered listener chain
 *
 * Events flow: GLFW poll → callbacks → InputManager → listeners (immediately!)
 * Each listener returns Reject/Used/Consumed to control propagation.
 *
 * IMPORTANT: The application MUST call window->pollEvents() (or glfwPollEvents())
 * each frame to pump events. Listeners are called synchronously DURING the poll,
 * not queued for later. By the time pollEvents() returns, all listeners have run.
 *
 * Usage:
 * @code
 * auto input = InputManager::create(window);
 *
 * // Register listeners with priorities (lower = higher precedence)
 * input->addListener([](const InputEvent& e) {
 *     if (guiHasFocus && e.type == InputEventType::KeyPress) {
 *         return ListenerResult::Consumed;  // GUI eats all keys
 *     }
 *     return ListenerResult::Reject;  // Pass to game
 * }, InputPriority::TextInput);
 *
 * input->addListener([](const InputEvent& e) {
 *     if (e.type == InputEventType::KeyPress && e.key == GLFW_KEY_W) {
 *         player.moveForward();
 *         return ListenerResult::Consumed;
 *     }
 *     return ListenerResult::Reject;
 * }, InputPriority::Game);
 *
 * // Game loop:
 * while (window->isOpen()) {
 *     window->pollEvents();    // Pumps GLFW → listeners run HERE
 *     input->update();         // Clear per-frame state (mouseDelta, etc.)
 *     // ... game logic, rendering
 * }
 *
 * // Direct state queries (polling between events)
 * if (input->isKeyDown(GLFW_KEY_W)) { ... }
 * @endcode
 */
class InputManager {
public:

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
     * Clears per-frame state (keysPressed, keysReleased, mouseDelta, scrollDelta).
     * Events are dispatched immediately to listeners as they occur from GLFW.
     */
    void update();

    // ========================================================================
    // Listener chain (priority-ordered event dispatch)
    // ========================================================================

    /**
     * @brief Register an input listener with a priority
     *
     * Listeners are called in priority order (lower numbers first). Each listener
     * returns Reject/Used/Consumed to control propagation.
     *
     * @param listener Function receiving InputEvent, returns ListenerResult
     * @param priority Priority value (use InputPriority:: constants or custom)
     * @return Listener ID for later removal
     */
    int addListener(InputListener listener, int priority);

    /**
     * @brief Remove a previously registered listener
     * @param id Listener ID returned by addListener()
     */
    void removeListener(int id);

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
    // Cursor mode
    // ========================================================================

    /**
     * @brief Set cursor visibility and positioning mode
     *
     * - Normal: Visible cursor, absolute screen coordinates
     * - Hidden: Invisible cursor, absolute coordinates still work (tooltips)
     * - Disabled: Locked/hidden cursor for camera control, use mouseDelta
     *
     * @param mode Cursor mode to set
     */
    void setCursorMode(CursorMode mode);

    /**
     * @brief Get current cursor mode
     * @return Current cursor mode (also available in state.cursorMode)
     */
    [[nodiscard]] CursorMode cursorMode() const { return state_.cursorMode; }

    // Deprecated: Use setCursorMode(CursorMode::Disabled) instead
    [[deprecated("Use setCursorMode(CursorMode::Disabled)")]]
    void setMouseCaptured(bool captured) {
        setCursorMode(captured ? CursorMode::Disabled : CursorMode::Normal);
    }

    [[deprecated("Use cursorMode() == CursorMode::Disabled")]]
    [[nodiscard]] bool isMouseCaptured() const {
        return state_.cursorMode == CursorMode::Disabled;
    }

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
    void handleKeyEvent(Key key, int scancode, Action action, Modifier mods);
    void handleMouseButtonEvent(MouseButton button, Action action, Modifier mods);
    void handleMouseMoveEvent(double x, double y);
    void handleScrollEvent(double xoffset, double yoffset);
    void handleCharEvent(uint32_t codepoint);

    void dispatchEvent(const InputEvent& event);
    double currentTime() const;

    Window* window_ = nullptr;

    // Current state (includes cursorMode, not separate mouseCaptured_)
    InputState state_;

    // Per-frame tracking (cleared in update())
    std::unordered_set<Key> keysPressed_;      // Keys pressed THIS frame
    std::unordered_set<Key> keysReleased_;     // Keys released THIS frame
    std::unordered_set<MouseButton> buttonsPressed_;
    std::unordered_set<MouseButton> buttonsReleased_;
    glm::vec2 lastMousePosition_{0.0f};

    // Listener chain (sorted by priority)
    struct ListenerEntry {
        int id;
        int priority;
        InputListener listener;
    };
    std::vector<ListenerEntry> listeners_;
    int nextListenerId_ = 1;

    // Action mappings
    std::unordered_map<std::string, Key> keyActions_;
    std::unordered_map<std::string, MouseButton> mouseActions_;

    // Timing
    std::chrono::steady_clock::time_point startTime_;
};

} // namespace finevk
