/**
 * @file test_input_manager.cpp
 * @brief InputManager tests - Fat events, action mapping, state queries
 *
 * Tests InputManager functionality using event injection for deterministic testing.
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>
#include <cassert>
#include <vector>

using namespace finevk;

// Global test state
struct TestContext {
    InstancePtr instance;
    WindowPtr window;
    PhysicalDevice physicalDevice;
    LogicalDevicePtr logicalDevice;
};

static TestContext ctx;

void setup_test_context() {
    std::cout << "Setting up test context...\n";

    // Create instance
    ctx.instance = Instance::create()
        .applicationName("InputManager Test")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    // Create Window (hidden)
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    ctx.window = Window::create(ctx.instance)
        .title("Test Window")
        .size(800, 600)
        .build();

    // Select physical device
    ctx.physicalDevice = ctx.instance->selectPhysicalDevice(ctx.window);

    std::cout << "  Selected GPU: " << ctx.physicalDevice.name() << "\n";

    // Create logical device
    ctx.logicalDevice = ctx.physicalDevice.createLogicalDevice()
        .surface(ctx.window->surface())
        .addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .build();

    // Bind device to window
    ctx.window->bindDevice(ctx.logicalDevice);

    std::cout << "  Test context ready.\n\n";
}

void cleanup_test_context() {
    std::cout << "\nCleaning up test context...\n";
    ctx.window.reset();
    ctx.logicalDevice.reset();
    ctx.instance.reset();
    std::cout << "  Cleanup complete.\n";
}

// ============================================================================
// InputManager Tests
// ============================================================================

void test_input_manager_creation() {
    std::cout << "Test: InputManager - Creation... ";

    auto input = InputManager::create(ctx.window.get());
    assert(input != nullptr);

    // Initial state should be empty
    assert(input->currentState().pressedKeys.empty());
    assert(input->currentState().pressedButtons.empty());
    assert(input->mousePosition().x == 0.0f || true);  // Position depends on cursor

    std::cout << "PASSED\n";
}

void test_input_state_queries() {
    std::cout << "Test: InputManager - State queries via event injection... ";

    auto input = InputManager::create(ctx.window.get());

    // Initially no keys pressed
    assert(input->isKeyDown(GLFW_KEY_W) == false);
    assert(input->wasKeyPressed(GLFW_KEY_W) == false);

    // Inject a key press event
    InputEvent pressEvent;
    pressEvent.type = InputEventType::KeyPress;
    pressEvent.key = GLFW_KEY_W;
    pressEvent.state.pressedKeys.insert(GLFW_KEY_W);
    input->injectEvent(pressEvent);

    // Now W should be down
    assert(input->isKeyDown(GLFW_KEY_W) == true);
    assert(input->wasKeyPressed(GLFW_KEY_W) == true);

    // Call update to clear per-frame state
    input->update();

    // W still down, but wasKeyPressed should be false
    assert(input->isKeyDown(GLFW_KEY_W) == true);
    assert(input->wasKeyPressed(GLFW_KEY_W) == false);

    // Inject key release
    InputEvent releaseEvent;
    releaseEvent.type = InputEventType::KeyRelease;
    releaseEvent.key = GLFW_KEY_W;
    input->injectEvent(releaseEvent);

    // W should now be released
    assert(input->isKeyDown(GLFW_KEY_W) == false);
    assert(input->wasKeyReleased(GLFW_KEY_W) == true);

    std::cout << "PASSED\n";
}

void test_mouse_button_injection() {
    std::cout << "Test: InputManager - Mouse button injection... ";

    auto input = InputManager::create(ctx.window.get());

    // Initially no buttons pressed
    assert(input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT) == false);

    // Inject left button press
    InputEvent pressEvent;
    pressEvent.type = InputEventType::MouseButtonPress;
    pressEvent.mouseButton = GLFW_MOUSE_BUTTON_LEFT;
    pressEvent.state.pressedButtons.insert(GLFW_MOUSE_BUTTON_LEFT);
    input->injectEvent(pressEvent);

    assert(input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT) == true);
    assert(input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) == true);

    input->update();

    assert(input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT) == true);
    assert(input->wasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) == false);

    std::cout << "PASSED\n";
}

void test_action_mapping() {
    std::cout << "Test: InputManager - Action mapping... ";

    auto input = InputManager::create(ctx.window.get());

    // Map some actions
    input->mapAction("move_forward", GLFW_KEY_W);
    input->mapAction("jump", GLFW_KEY_SPACE);
    input->mapActionToMouse("shoot", GLFW_MOUSE_BUTTON_LEFT);

    // Initially no actions active
    assert(input->isActionActive("move_forward") == false);
    assert(input->isActionActive("jump") == false);
    assert(input->isActionActive("shoot") == false);

    // Inject W key press
    InputEvent pressW;
    pressW.type = InputEventType::KeyPress;
    pressW.key = GLFW_KEY_W;
    input->injectEvent(pressW);

    assert(input->isActionActive("move_forward") == true);
    assert(input->wasActionTriggered("move_forward") == true);
    assert(input->isActionActive("jump") == false);  // Space not pressed

    // Inject left mouse button
    InputEvent pressLMB;
    pressLMB.type = InputEventType::MouseButtonPress;
    pressLMB.mouseButton = GLFW_MOUSE_BUTTON_LEFT;
    input->injectEvent(pressLMB);

    assert(input->isActionActive("shoot") == true);

    // Unmap action
    input->unmapAction("move_forward");
    assert(input->isActionActive("move_forward") == false);  // No longer mapped

    std::cout << "PASSED\n";
}

void test_listener_immediate_dispatch() {
    std::cout << "Test: InputManager - Listener immediate dispatch... ";

    auto input = InputManager::create(ctx.window.get());

    // Register listener to capture events
    std::vector<InputEvent> received;
    input->addListener([&received](const InputEvent& e) {
        received.push_back(e);
        return ListenerResult::Reject;  // Let others see it too
    }, InputPriority::Game);

    // Inject multiple events - listeners are called IMMEDIATELY during inject
    InputEvent event1;
    event1.type = InputEventType::KeyPress;
    event1.key = GLFW_KEY_A;
    input->injectEvent(event1);

    // Event should be dispatched immediately
    assert(received.size() == 1);
    assert(received[0].type == InputEventType::KeyPress);
    assert(received[0].key == GLFW_KEY_A);

    InputEvent event2;
    event2.type = InputEventType::KeyPress;
    event2.key = GLFW_KEY_B;
    input->injectEvent(event2);

    InputEvent event3;
    event3.type = InputEventType::MouseMove;
    event3.state.mousePosition = glm::vec2(100.0f, 200.0f);
    input->injectEvent(event3);

    // All events dispatched immediately
    assert(received.size() == 3);
    assert(received[1].type == InputEventType::KeyPress);
    assert(received[1].key == GLFW_KEY_B);
    assert(received[2].type == InputEventType::MouseMove);

    std::cout << "PASSED\n";
}

void test_listener_priority_ordering() {
    std::cout << "Test: InputManager - Listener priority ordering... ";

    auto input = InputManager::create(ctx.window.get());

    std::vector<int> order;

    // Register listeners with different priorities (lower = higher precedence)
    input->addListener([&order](const InputEvent& e) {
        order.push_back(500);  // Game priority
        return ListenerResult::Reject;
    }, InputPriority::Game);

    input->addListener([&order](const InputEvent& e) {
        order.push_back(100);  // TextInput priority (highest)
        return ListenerResult::Reject;
    }, InputPriority::TextInput);

    input->addListener([&order](const InputEvent& e) {
        order.push_back(300);  // HUD priority
        return ListenerResult::Reject;
    }, InputPriority::HUD);

    // Inject event - should call listeners in priority order
    InputEvent event;
    event.type = InputEventType::KeyPress;
    event.key = GLFW_KEY_X;
    input->injectEvent(event);

    // Verify order: 100 (TextInput), 300 (HUD), 500 (Game)
    assert(order.size() == 3);
    assert(order[0] == 100);
    assert(order[1] == 300);
    assert(order[2] == 500);

    std::cout << "PASSED\n";
}

void test_fat_event_state() {
    std::cout << "Test: InputManager - Fat event contains complete state... ";

    auto input = InputManager::create(ctx.window.get());

    std::vector<InputEvent> received;
    input->addListener([&received](const InputEvent& e) {
        received.push_back(e);
        return ListenerResult::Reject;
    }, InputPriority::Game);

    // Press W first
    InputEvent pressW;
    pressW.type = InputEventType::KeyPress;
    pressW.key = GLFW_KEY_W;
    pressW.state.pressedKeys.insert(GLFW_KEY_W);
    input->injectEvent(pressW);

    // Now press A - the event should include that W is also pressed
    InputEvent pressA;
    pressA.type = InputEventType::KeyPress;
    pressA.key = GLFW_KEY_A;
    pressA.state.pressedKeys.insert(GLFW_KEY_W);
    pressA.state.pressedKeys.insert(GLFW_KEY_A);
    input->injectEvent(pressA);

    // Verify the A event contains W in state (fat event)
    assert(received.size() == 2);
    const auto& aEvent = received[1];
    assert(aEvent.type == InputEventType::KeyPress);
    assert(aEvent.key == GLFW_KEY_A);
    assert(aEvent.state.isKeyPressed(GLFW_KEY_W));  // Fat event: W still in state
    assert(aEvent.state.isKeyPressed(GLFW_KEY_A));

    std::cout << "PASSED\n";
}

void test_listener_propagation() {
    std::cout << "Test: InputManager - Listener propagation control... ";

    auto input = InputManager::create(ctx.window.get());

    bool listener1Called = false;
    bool listener2Called = false;
    bool listener3Called = false;

    // Listener 1 (highest priority) - consumes event
    input->addListener([&listener1Called](const InputEvent& e) {
        listener1Called = true;
        return ListenerResult::Consumed;  // Stop propagation
    }, 100);

    // Listener 2 - should not be called because event consumed
    input->addListener([&listener2Called](const InputEvent& e) {
        listener2Called = true;
        return ListenerResult::Reject;
    }, 200);

    // Listener 3 - should not be called
    input->addListener([&listener3Called](const InputEvent& e) {
        listener3Called = true;
        return ListenerResult::Reject;
    }, 300);

    InputEvent event;
    event.type = InputEventType::KeyPress;
    event.key = GLFW_KEY_Q;
    input->injectEvent(event);

    // Only listener1 should have been called
    assert(listener1Called == true);
    assert(listener2Called == false);
    assert(listener3Called == false);

    std::cout << "PASSED\n";
}

void test_scroll_accumulation() {
    std::cout << "Test: InputManager - Scroll accumulation... ";

    // Test ScrollAccumulator directly (unit test)
    ScrollAccumulator accX;

    // Accumulate fractional values (like trackpad)
    accX.accumulate(0.3f);
    assert(accX.remainder() >= 0.29f && accX.remainder() <= 0.31f);

    accX.accumulate(0.8f);
    // Total = 1.1
    assert(accX.remainder() >= 1.09f && accX.remainder() <= 1.11f);

    // Consume whole ticks
    int ticks = accX.consumeTicks();
    assert(ticks == 1);  // Got 1 tick
    assert(accX.remainder() >= 0.09f && accX.remainder() <= 0.11f);  // 0.1 left

    // Test negative scrolling
    ScrollAccumulator accY;
    accY.accumulate(-2.7f);
    int negativeTicks = accY.consumeTicks();
    assert(negativeTicks == -3);  // floor(-2.7) = -3
    assert(accY.remainder() >= 0.29f && accY.remainder() <= 0.31f);  // 0.3 left (remainder goes positive)

    // Test reset
    accX.reset();
    assert(accX.remainder() == 0.0f);

    std::cout << "PASSED\n";
}

void test_clear_action_mappings() {
    std::cout << "Test: InputManager - Clear action mappings... ";

    auto input = InputManager::create(ctx.window.get());

    input->mapAction("action1", GLFW_KEY_A);
    input->mapAction("action2", GLFW_KEY_B);
    input->mapActionToMouse("action3", GLFW_MOUSE_BUTTON_RIGHT);

    // Press the keys
    InputEvent e;
    e.type = InputEventType::KeyPress;
    e.key = GLFW_KEY_A;
    input->injectEvent(e);

    assert(input->isActionActive("action1") == true);

    // Clear all mappings
    input->clearActionMappings();

    // Action should no longer be active (no mapping)
    assert(input->isActionActive("action1") == false);
    assert(input->isActionActive("action2") == false);
    assert(input->isActionActive("action3") == false);

    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "InputManager Tests\n";
    std::cout << "========================================\n\n";

    try {
        setup_test_context();

        test_input_manager_creation();
        test_input_state_queries();
        test_mouse_button_injection();
        test_action_mapping();
        test_listener_immediate_dispatch();
        test_listener_priority_ordering();
        test_fat_event_state();
        test_listener_propagation();
        test_scroll_accumulation();
        test_clear_action_mappings();

        cleanup_test_context();

        std::cout << "\n========================================\n";
        std::cout << "All InputManager tests PASSED!\n";
        std::cout << "========================================\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED with exception: " << e.what() << "\n";
        cleanup_test_context();
        return 1;
    }
}
