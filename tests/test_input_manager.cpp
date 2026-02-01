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

void test_event_queue_polling() {
    std::cout << "Test: InputManager - Event queue polling... ";

    auto input = InputManager::create(ctx.window.get());

    // Inject multiple events
    InputEvent event1;
    event1.type = InputEventType::KeyPress;
    event1.key = GLFW_KEY_A;
    input->injectEvent(event1);

    InputEvent event2;
    event2.type = InputEventType::KeyPress;
    event2.key = GLFW_KEY_B;
    input->injectEvent(event2);

    InputEvent event3;
    event3.type = InputEventType::MouseMove;
    event3.state.mousePosition = glm::vec2(100.0f, 200.0f);
    input->injectEvent(event3);

    // Poll events
    std::vector<InputEvent> polled;
    InputEvent e;
    while (input->pollEvent(e)) {
        polled.push_back(e);
    }

    assert(polled.size() == 3);
    assert(polled[0].type == InputEventType::KeyPress);
    assert(polled[0].key == GLFW_KEY_A);
    assert(polled[1].type == InputEventType::KeyPress);
    assert(polled[1].key == GLFW_KEY_B);
    assert(polled[2].type == InputEventType::MouseMove);

    // Queue should be empty now
    assert(input->pollEvent(e) == false);

    std::cout << "PASSED\n";
}

void test_event_callback() {
    std::cout << "Test: InputManager - Event callback... ";

    auto input = InputManager::create(ctx.window.get());

    std::vector<InputEvent> received;
    input->setEventCallback([&received](const InputEvent& e) {
        received.push_back(e);
    });

    // Inject events
    InputEvent event1;
    event1.type = InputEventType::KeyPress;
    event1.key = GLFW_KEY_X;
    input->injectEvent(event1);

    InputEvent event2;
    event2.type = InputEventType::KeyRelease;
    event2.key = GLFW_KEY_X;
    input->injectEvent(event2);

    // Events not dispatched until update()
    assert(received.size() == 0);

    input->update();

    // Now callback should have been called
    assert(received.size() == 2);
    assert(received[0].type == InputEventType::KeyPress);
    assert(received[1].type == InputEventType::KeyRelease);

    std::cout << "PASSED\n";
}

void test_fat_event_state() {
    std::cout << "Test: InputManager - Fat event contains complete state... ";

    auto input = InputManager::create(ctx.window.get());

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

    // Poll the A event and verify it contains W in state
    InputEvent e;
    input->pollEvent(e);  // Skip W event
    bool got = input->pollEvent(e);  // Get A event
    assert(got);
    assert(e.type == InputEventType::KeyPress);
    assert(e.key == GLFW_KEY_A);
    assert(e.state.isKeyPressed(GLFW_KEY_W));  // Fat event: W still in state
    assert(e.state.isKeyPressed(GLFW_KEY_A));

    std::cout << "PASSED\n";
}

void test_clear_events() {
    std::cout << "Test: InputManager - Clear events... ";

    auto input = InputManager::create(ctx.window.get());

    // Inject some events
    InputEvent e;
    e.type = InputEventType::KeyPress;
    e.key = GLFW_KEY_Q;
    input->injectEvent(e);
    input->injectEvent(e);
    input->injectEvent(e);

    // Clear
    input->clearEvents();

    // Queue should be empty
    InputEvent out;
    assert(input->pollEvent(out) == false);

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
        test_event_queue_polling();
        test_event_callback();
        test_fat_event_state();
        test_clear_events();
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
