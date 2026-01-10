#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/physical_device.hpp"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <optional>

namespace finevk {

class Instance;
class Surface;
class LogicalDevice;
class SwapChain;
class Queue;

/**
 * @brief Keyboard key codes - uses GLFW key constants directly
 *
 * All GLFW_KEY_* constants can be used. Common ones include:
 * - Letters: GLFW_KEY_A through GLFW_KEY_Z
 * - Numbers: GLFW_KEY_0 through GLFW_KEY_9
 * - Function keys: GLFW_KEY_F1 through GLFW_KEY_F25
 * - Special: GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_SPACE, etc.
 * - Arrows: GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN
 * - Modifiers: GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_ALT, etc.
 *
 * See https://www.glfw.org/docs/latest/group__keys.html for full list.
 */
using Key = int;

/**
 * @brief Mouse button codes - uses GLFW button constants directly
 *
 * All GLFW_MOUSE_BUTTON_* constants can be used:
 * - GLFW_MOUSE_BUTTON_LEFT (0)
 * - GLFW_MOUSE_BUTTON_RIGHT (1)
 * - GLFW_MOUSE_BUTTON_MIDDLE (2)
 * - GLFW_MOUSE_BUTTON_4 through GLFW_MOUSE_BUTTON_8
 *
 * See https://www.glfw.org/docs/latest/group__buttons.html for full list.
 */
using MouseButton = int;

/**
 * @brief Input action states
 */
enum class Action {
    Release,
    Press,
    Repeat
};

/**
 * @brief Modifier key flags
 */
enum class Modifier : uint32_t {
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3,
    CapsLock = 1 << 4,
    NumLock = 1 << 5
};

inline Modifier operator|(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(Modifier a, Modifier b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

/**
 * @brief Window configuration options
 */
struct WindowConfig {
    std::string title = "FineStructure Application";
    uint32_t width = 800;
    uint32_t height = 600;
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
    uint32_t framesInFlight = 2;
};

/**
 * @brief Result of acquiring the next frame for rendering
 */
struct FrameInfo {
    uint32_t imageIndex = 0;          // Which swap chain image we're rendering to
    uint32_t frameIndex = 0;          // Which frame-in-flight (for per-frame resources)
    VkExtent2D extent{};              // Current window size
    VkImage image = VK_NULL_HANDLE;   // The swap chain image to render to
    VkImageView imageView = VK_NULL_HANDLE;  // View for the swap chain image

    // Synchronization for this frame (user typically doesn't need these directly)
    VkSemaphore imageAvailable = VK_NULL_HANDLE;  // Signaled when image acquired
    VkSemaphore renderFinished = VK_NULL_HANDLE;  // Signal when done rendering
    VkFence inFlightFence = VK_NULL_HANDLE;       // CPU-GPU sync for this frame
};

/**
 * @brief Platform-abstracted window with Vulkan surface and swap chain
 *
 * Window encapsulates:
 * - Platform window (GLFW internally)
 * - Vulkan surface
 * - Swap chain (created automatically)
 * - Frame synchronization
 * - Input event handling
 *
 * Usage:
 * @code
 * auto window = instance->createWindow()
 *     .title("My Game")
 *     .size(1280, 720)
 *     .build();
 *
 * while (window->isOpen()) {
 *     window->pollEvents();
 *
 *     if (auto frame = window->beginFrame()) {
 *         // Render using frame->imageIndex, frame->frameIndex
 *         window->endFrame();
 *     }
 * }
 * @endcode
 */
class Window {
public:
    /**
     * @brief Builder for creating Window objects
     */
    class Builder {
    public:
        explicit Builder(Instance* instance);

        /// Set window title
        Builder& title(std::string_view title);

        /// Set window size
        Builder& size(uint32_t width, uint32_t height);

        /// Enable/disable resizing
        Builder& resizable(bool enabled = true);

        /// Enable/disable fullscreen
        Builder& fullscreen(bool enabled = true);

        /// Enable/disable vsync
        Builder& vsync(bool enabled = true);

        /// Set frames in flight (for swap chain)
        Builder& framesInFlight(uint32_t count);

        /// Build the window
        WindowPtr build();

    private:
        Instance* instance_;
        WindowConfig config_;
    };

    /// Create a builder for a window
    static Builder create(Instance* instance);
    static Builder create(Instance& instance) { return create(&instance); }
    static Builder create(const InstancePtr& instance) { return create(instance.get()); }

    // ========================================================================
    // Window state
    // ========================================================================

    /// Check if the window is still open
    bool isOpen() const;

    /// Request the window to close
    void close();

    /// Get current window size
    glm::uvec2 size() const;
    uint32_t width() const;
    uint32_t height() const;

    /// Get the window title
    const std::string& title() const { return config_.title; }

    /// Set window title
    void setTitle(std::string_view title);

    /// Check if window is minimized
    bool isMinimized() const;

    /// Check if window is focused
    bool isFocused() const;

    // ========================================================================
    // Device and surface access
    // ========================================================================

    /// Get the parent instance
    Instance* instance() const { return instance_; }

    /// Get the Vulkan surface
    Surface* surface() const { return surface_.get(); }

    /// Get the swap chain
    SwapChain* swapChain() const { return swapChain_.get(); }

    /// Get current swap chain extent
    VkExtent2D extent() const;

    /// Get swap chain image format
    VkFormat format() const;

    // ========================================================================
    // Device binding
    // ========================================================================

    /**
     * @brief Bind this window to a logical device
     *
     * This creates the swap chain and synchronization objects. Must be called
     * before beginFrame(). The window keeps a non-owning reference to the device.
     *
     * @param device The logical device to use for rendering
     */
    void bindDevice(LogicalDevice& device);
    void bindDevice(LogicalDevice* device) { bindDevice(*device); }
    void bindDevice(const LogicalDevicePtr& device) { bindDevice(*device); }

    /// Check if a device is bound
    bool hasDevice() const { return device_ != nullptr; }

    /// Get the bound device (nullptr if not bound)
    LogicalDevice* device() const { return device_; }

    /**
     * @brief Release all device-dependent resources
     *
     * Call this before the device is destroyed if the window outlives the device.
     * After calling this, hasDevice() will return false and beginFrame() will fail.
     * This is called automatically in the destructor.
     */
    void releaseDeviceResources();

    /// Get the number of frames in flight
    uint32_t framesInFlight() const { return config_.framesInFlight; }

    /// Get the current frame index (0 to framesInFlight-1)
    uint32_t currentFrame() const { return currentFrameIndex_; }

    /// Get the current swap chain image index (set by beginFrame)
    uint32_t currentImageIndex() const { return currentImageIndex_; }

    // ========================================================================
    // Frame lifecycle
    // ========================================================================

    /**
     * @brief Begin a new frame
     *
     * Waits for the previous frame using this slot to complete, then acquires
     * the next swap chain image. Returns frame information including sync objects.
     *
     * Returns std::nullopt if the window is minimized or being resized.
     * In that case, just skip the frame and call beginFrame() again next iteration.
     *
     * @return Frame information, or nullopt if frame should be skipped
     */
    std::optional<FrameInfo> beginFrame();

    /**
     * @brief Present the current frame
     *
     * Call this after submitting your rendering commands. The command buffer
     * submission should wait on frame.imageAvailable and signal frame.renderFinished.
     *
     * @return true if presentation succeeded
     */
    bool endFrame();

    /**
     * @brief Wait for all rendering to complete
     *
     * Call before destroying resources or exiting.
     */
    void waitIdle();

    // ========================================================================
    // Event handling
    // ========================================================================

    /// Process pending window events (call once per frame)
    void pollEvents();

    /// Wait for events (blocks until an event occurs)
    void waitEvents();

    // Callback-based events
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
    using KeyCallback = std::function<void(Key key, Action action, Modifier mods)>;
    using MouseButtonCallback = std::function<void(MouseButton button, Action action, Modifier mods)>;
    using MouseMoveCallback = std::function<void(double x, double y)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    using CharCallback = std::function<void(uint32_t codepoint)>;

    void onResize(ResizeCallback callback) { resizeCallback_ = std::move(callback); }
    void onKey(KeyCallback callback) { keyCallback_ = std::move(callback); }
    void onMouseButton(MouseButtonCallback callback) { mouseButtonCallback_ = std::move(callback); }
    void onMouseMove(MouseMoveCallback callback) { mouseMoveCallback_ = std::move(callback); }
    void onScroll(ScrollCallback callback) { scrollCallback_ = std::move(callback); }
    void onChar(CharCallback callback) { charCallback_ = std::move(callback); }

    // Polling-based input
    bool isKeyPressed(Key key) const;
    bool isKeyReleased(Key key) const;
    bool isMouseButtonPressed(MouseButton button) const;
    glm::dvec2 mousePosition() const;

    /// Set mouse capture mode (hides cursor and locks to window)
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Destructor
    ~Window();

    // Non-copyable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Movable
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

private:
    friend class Builder;
    friend class Instance;
    Window() = default;

    void createSurface();
    void createSwapChain();
    void createSyncObjects();
    void setupCallbacks();
    void cleanup();
    void cleanupSwapChain();
    void recreateSwapChain();

    // Static GLFW callbacks (forward to instance methods)
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwCursorPosCallback(GLFWwindow* window, double x, double y);
    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void glfwCharCallback(GLFWwindow* window, unsigned int codepoint);
    static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);

    // Modifier conversion (Key and MouseButton use GLFW constants directly)
    static Modifier glfwModsToModifier(int glfwMods);

    // Core state
    Instance* instance_ = nullptr;
    LogicalDevice* device_ = nullptr;  // Non-owning, set by bindDevice()
    GLFWwindow* window_ = nullptr;
    WindowConfig config_;

    // Vulkan objects
    SurfacePtr surface_;
    SwapChainPtr swapChain_;

    // Synchronization (one set per frame in flight)
    std::vector<SemaphorePtr> imageAvailableSemaphores_;
    std::vector<SemaphorePtr> renderFinishedSemaphores_;
    std::vector<FencePtr> inFlightFences_;

    // Frame state
    uint32_t currentFrameIndex_ = 0;
    uint32_t currentImageIndex_ = 0;
    bool framebufferResized_ = false;

    // Device destruction callback registration
    size_t deviceDestructionCallbackId_ = 0;

    // Event callbacks
    ResizeCallback resizeCallback_;
    KeyCallback keyCallback_;
    MouseButtonCallback mouseButtonCallback_;
    MouseMoveCallback mouseMoveCallback_;
    ScrollCallback scrollCallback_;
    CharCallback charCallback_;
};

} // namespace finevk
