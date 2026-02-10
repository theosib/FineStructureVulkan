#include "finevk/engine/input_manager.hpp"

#include <algorithm>

namespace finevk {

// ============================================================================
// Construction / Destruction
// ============================================================================

std::unique_ptr<InputManager> InputManager::create(Window* window) {
    // Use unique_ptr with custom construction (private constructor)
    return std::unique_ptr<InputManager>(new InputManager(window));
}

InputManager::InputManager(Window* window)
    : window_(window)
    , startTime_(std::chrono::steady_clock::now())
{
    if (window_) {
        // Get initial mouse position
        auto pos = window_->mousePosition();
        state_.mousePosition = glm::vec2(static_cast<float>(pos.x), static_cast<float>(pos.y));
        lastMousePosition_ = state_.mousePosition;

        setupCallbacks();
    }
}

InputManager::~InputManager() {
    // Clear callbacks to avoid dangling references
    if (window_) {
        window_->onKey(nullptr);
        window_->onMouseButton(nullptr);
        window_->onMouseMove(nullptr);
        window_->onScroll(nullptr);
        window_->onChar(nullptr);
    }
}

// ============================================================================
// Callback Setup
// ============================================================================

void InputManager::setupCallbacks() {
    window_->onKey([this](Key key, int scancode, Action action, Modifier mods) {
        handleKeyEvent(key, scancode, action, mods);
    });

    window_->onMouseButton([this](MouseButton button, Action action, Modifier mods) {
        handleMouseButtonEvent(button, action, mods);
    });

    window_->onMouseMove([this](double x, double y) {
        handleMouseMoveEvent(x, y);
    });

    window_->onScroll([this](double xoffset, double yoffset) {
        handleScrollEvent(xoffset, yoffset);
    });

    window_->onChar([this](uint32_t codepoint) {
        handleCharEvent(codepoint);
    });
}

// ============================================================================
// Event Handlers
// ============================================================================

void InputManager::handleKeyEvent(Key key, int scancode, Action action, Modifier mods) {
    state_.modifiers = mods;

    InputEvent event;
    event.key = key;
    event.scancode = scancode;
    event.time = currentTime();

    switch (action) {
        case Action::Press:
            state_.pressedKeys.insert(key);
            keysPressed_.insert(key);
            event.type = InputEventType::KeyPress;
            break;

        case Action::Release:
            state_.pressedKeys.erase(key);
            keysReleased_.insert(key);
            event.type = InputEventType::KeyRelease;
            break;

        case Action::Repeat:
            event.type = InputEventType::KeyRepeat;
            break;
    }

    event.state = state_;
    dispatchEvent(event);
}

void InputManager::handleMouseButtonEvent(MouseButton button, Action action, Modifier mods) {
    state_.modifiers = mods;

    InputEvent event;
    event.mouseButton = button;
    event.time = currentTime();

    switch (action) {
        case Action::Press:
            state_.pressedButtons.insert(button);
            buttonsPressed_.insert(button);
            event.type = InputEventType::MouseButtonPress;
            break;

        case Action::Release:
            state_.pressedButtons.erase(button);
            buttonsReleased_.insert(button);
            event.type = InputEventType::MouseButtonRelease;
            break;

        case Action::Repeat:
            // Mouse buttons don't typically repeat, but handle it
            event.type = InputEventType::MouseButtonPress;
            break;
    }

    event.state = state_;
    dispatchEvent(event);
}

void InputManager::handleMouseMoveEvent(double x, double y) {
    glm::vec2 newPos(static_cast<float>(x), static_cast<float>(y));

    // Calculate delta from last position
    glm::vec2 delta = newPos - lastMousePosition_;
    lastMousePosition_ = newPos;

    // Update state
    state_.mousePosition = newPos;
    state_.mouseDelta += delta;  // Accumulate until update() clears it

    InputEvent event;
    event.type = InputEventType::MouseMove;
    event.time = currentTime();
    event.state = state_;
    dispatchEvent(event);
}

void InputManager::handleScrollEvent(double xoffset, double yoffset) {
    // Store raw fractional deltas
    state_.scrollDelta.x = static_cast<float>(xoffset);
    state_.scrollDelta.y = static_cast<float>(yoffset);

    // Auto-accumulate for discrete ticks
    state_.scrollAccumulatorX.accumulate(static_cast<float>(xoffset));
    state_.scrollAccumulatorY.accumulate(static_cast<float>(yoffset));

    InputEvent event;
    event.type = InputEventType::MouseScroll;
    event.time = currentTime();
    event.state = state_;
    dispatchEvent(event);
}

void InputManager::handleCharEvent(uint32_t codepoint) {
    InputEvent event;
    event.type = InputEventType::CharInput;
    event.character = codepoint;
    event.time = currentTime();
    event.state = state_;
    dispatchEvent(event);
}

// ============================================================================
// Listener Chain
// ============================================================================

int InputManager::addListener(InputListener listener, int priority) {
    int id = nextListenerId_++;
    listeners_.push_back({id, priority, std::move(listener)});

    // Keep listeners sorted by priority (lower = higher precedence)
    std::sort(listeners_.begin(), listeners_.end(),
        [](const ListenerEntry& a, const ListenerEntry& b) {
            return a.priority < b.priority;
        });

    return id;
}

void InputManager::removeListener(int id) {
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [id](const ListenerEntry& entry) { return entry.id == id; }),
        listeners_.end());
}

void InputManager::dispatchEvent(const InputEvent& event) {
    for (const auto& entry : listeners_) {
        ListenerResult result = entry.listener(event);
        if (result == ListenerResult::Consumed) {
            break;  // Stop propagation
        }
        // Reject and Used both continue to next listener
    }
}

// ============================================================================
// Frame Update
// ============================================================================

void InputManager::update() {
    // Clear per-frame state for next frame
    // Events are dispatched immediately to listeners as they occur
    keysPressed_.clear();
    keysReleased_.clear();
    buttonsPressed_.clear();
    buttonsReleased_.clear();
    state_.mouseDelta = glm::vec2(0.0f);
    state_.scrollDelta = glm::vec2(0.0f);
}

// ============================================================================
// Cursor Mode
// ============================================================================

void InputManager::setCursorMode(CursorMode mode) {
    state_.cursorMode = mode;
    if (window_) {
        // For now, map to Window's setMouseCaptured (Disabled = captured, others = not)
        // TODO: Extend Window with full CursorMode support (Normal/Hidden/Disabled)
        bool captured = (mode == CursorMode::Disabled);
        window_->setMouseCaptured(captured);

        if (captured) {
            // Reset last position to avoid jump when entering camera mode
            auto pos = window_->mousePosition();
            lastMousePosition_ = glm::vec2(static_cast<float>(pos.x), static_cast<float>(pos.y));
            state_.mousePosition = lastMousePosition_;
        }
    }
}

// ============================================================================
// Action Mapping
// ============================================================================

void InputManager::mapAction(const std::string& action, Key key) {
    keyActions_[action] = key;
    mouseActions_.erase(action);  // Remove any mouse mapping
}

void InputManager::mapActionToMouse(const std::string& action, MouseButton button) {
    mouseActions_[action] = button;
    keyActions_.erase(action);  // Remove any key mapping
}

bool InputManager::isActionActive(const std::string& action) const {
    // Check key actions
    auto keyIt = keyActions_.find(action);
    if (keyIt != keyActions_.end()) {
        return isKeyDown(keyIt->second);
    }

    // Check mouse actions
    auto mouseIt = mouseActions_.find(action);
    if (mouseIt != mouseActions_.end()) {
        return isMouseButtonDown(mouseIt->second);
    }

    return false;
}

bool InputManager::wasActionTriggered(const std::string& action) const {
    // Check key actions
    auto keyIt = keyActions_.find(action);
    if (keyIt != keyActions_.end()) {
        return wasKeyPressed(keyIt->second);
    }

    // Check mouse actions
    auto mouseIt = mouseActions_.find(action);
    if (mouseIt != mouseActions_.end()) {
        return wasMouseButtonPressed(mouseIt->second);
    }

    return false;
}

void InputManager::unmapAction(const std::string& action) {
    keyActions_.erase(action);
    mouseActions_.erase(action);
}

void InputManager::clearActionMappings() {
    keyActions_.clear();
    mouseActions_.clear();
}

// ============================================================================
// Simulation
// ============================================================================

void InputManager::injectEvent(const InputEvent& event) {
    // Apply event to state as if it came from hardware
    switch (event.type) {
        case InputEventType::KeyPress:
            state_.pressedKeys.insert(event.key);
            keysPressed_.insert(event.key);
            break;

        case InputEventType::KeyRelease:
            state_.pressedKeys.erase(event.key);
            keysReleased_.insert(event.key);
            break;

        case InputEventType::MouseButtonPress:
            state_.pressedButtons.insert(event.mouseButton);
            buttonsPressed_.insert(event.mouseButton);
            break;

        case InputEventType::MouseButtonRelease:
            state_.pressedButtons.erase(event.mouseButton);
            buttonsReleased_.insert(event.mouseButton);
            break;

        case InputEventType::MouseMove:
            state_.mousePosition = event.state.mousePosition;
            state_.mouseDelta = event.state.mouseDelta;
            break;

        case InputEventType::MouseScroll:
            state_.scrollDelta = event.state.scrollDelta;
            break;

        default:
            break;
    }

    // Dispatch the event to listeners
    dispatchEvent(event);
}

// ============================================================================
// Timing
// ============================================================================

double InputManager::currentTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - startTime_).count();
}

} // namespace finevk
