#include "finevk/window/window.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/rendering/swapchain.hpp"
#include "finevk/rendering/sync.hpp"
#include "finevk/core/logging.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace finevk {

// ============================================================================
// Builder implementation
// ============================================================================

Window::Builder::Builder(Instance* instance)
    : instance_(instance) {
}

Window::Builder& Window::Builder::title(std::string_view title) {
    config_.title = std::string(title);
    return *this;
}

Window::Builder& Window::Builder::size(uint32_t width, uint32_t height) {
    config_.width = width;
    config_.height = height;
    return *this;
}

Window::Builder& Window::Builder::resizable(bool enabled) {
    config_.resizable = enabled;
    return *this;
}

Window::Builder& Window::Builder::fullscreen(bool enabled) {
    config_.fullscreen = enabled;
    return *this;
}

Window::Builder& Window::Builder::vsync(bool enabled) {
    config_.vsync = enabled;
    return *this;
}

Window::Builder& Window::Builder::framesInFlight(uint32_t count) {
    config_.framesInFlight = count;
    return *this;
}

WindowPtr Window::Builder::build() {
    auto window = WindowPtr(new Window());
    window->instance_ = instance_;
    window->config_ = config_;

    // Initialize GLFW if needed
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Configure window hints
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, config_.resizable ? GLFW_TRUE : GLFW_FALSE);

    // Get monitor for fullscreen
    GLFWmonitor* monitor = nullptr;
    if (config_.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        config_.width = mode->width;
        config_.height = mode->height;
    }

    // Create the window
    window->window_ = glfwCreateWindow(
        static_cast<int>(config_.width),
        static_cast<int>(config_.height),
        config_.title.c_str(),
        monitor,
        nullptr);

    if (!window->window_) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Store pointer to Window instance for callbacks
    glfwSetWindowUserPointer(window->window_, window.get());

    // Setup callbacks
    window->setupCallbacks();

    // Create Vulkan surface
    window->createSurface();

    FINEVK_INFO(LogCategory::Core, "Window created: " + std::to_string(config_.width) + "x" +
                std::to_string(config_.height) + " \"" + config_.title + "\"");

    return window;
}

// ============================================================================
// Window implementation
// ============================================================================

Window::Builder Window::create(Instance* instance) {
    return Builder(instance);
}

Window::~Window() {
    cleanup();
}

Window::Window(Window&& other) noexcept
    : instance_(other.instance_)
    , device_(other.device_)
    , window_(other.window_)
    , config_(std::move(other.config_))
    , surface_(std::move(other.surface_))
    , swapChain_(std::move(other.swapChain_))
    , imageAvailableSemaphores_(std::move(other.imageAvailableSemaphores_))
    , renderFinishedSemaphores_(std::move(other.renderFinishedSemaphores_))
    , inFlightFences_(std::move(other.inFlightFences_))
    , currentFrameIndex_(other.currentFrameIndex_)
    , currentImageIndex_(other.currentImageIndex_)
    , framebufferResized_(other.framebufferResized_)
    , resizeCallback_(std::move(other.resizeCallback_))
    , keyCallback_(std::move(other.keyCallback_))
    , mouseButtonCallback_(std::move(other.mouseButtonCallback_))
    , mouseMoveCallback_(std::move(other.mouseMoveCallback_))
    , scrollCallback_(std::move(other.scrollCallback_))
    , charCallback_(std::move(other.charCallback_)) {
    other.window_ = nullptr;
    other.device_ = nullptr;
    if (window_) {
        glfwSetWindowUserPointer(window_, this);
    }
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        cleanup();

        instance_ = other.instance_;
        device_ = other.device_;
        window_ = other.window_;
        config_ = std::move(other.config_);
        surface_ = std::move(other.surface_);
        swapChain_ = std::move(other.swapChain_);
        imageAvailableSemaphores_ = std::move(other.imageAvailableSemaphores_);
        renderFinishedSemaphores_ = std::move(other.renderFinishedSemaphores_);
        inFlightFences_ = std::move(other.inFlightFences_);
        currentFrameIndex_ = other.currentFrameIndex_;
        currentImageIndex_ = other.currentImageIndex_;
        framebufferResized_ = other.framebufferResized_;
        resizeCallback_ = std::move(other.resizeCallback_);
        keyCallback_ = std::move(other.keyCallback_);
        mouseButtonCallback_ = std::move(other.mouseButtonCallback_);
        mouseMoveCallback_ = std::move(other.mouseMoveCallback_);
        scrollCallback_ = std::move(other.scrollCallback_);
        charCallback_ = std::move(other.charCallback_);

        other.window_ = nullptr;
        other.device_ = nullptr;
        if (window_) {
            glfwSetWindowUserPointer(window_, this);
        }
    }
    return *this;
}

void Window::releaseDeviceResources() {
    if (!device_) {
        return;  // Already released or never bound
    }

    // Unregister from device destruction notifications
    if (deviceDestructionCallbackId_ != 0) {
        device_->removeDestructionCallback(deviceDestructionCallbackId_);
        deviceDestructionCallbackId_ = 0;
    }

    // Wait for device before cleanup
    device_->waitIdle();

    // Cleanup sync objects first (they depend on device)
    inFlightFences_.clear();
    renderFinishedSemaphores_.clear();
    imageAvailableSemaphores_.clear();

    // Cleanup swap chain
    swapChain_.reset();

    // Mark device as unbound
    device_ = nullptr;

    FINEVK_DEBUG(LogCategory::Core, "Window device resources released");
}

void Window::cleanup() {
    // Release device resources if still bound
    // This handles the case where the device is still valid
    if (device_) {
        releaseDeviceResources();
    } else {
        // Device already gone (destroyed before window) - just clear our handles
        // The sync objects and swap chain are already invalid, just clear the pointers
        inFlightFences_.clear();
        renderFinishedSemaphores_.clear();
        imageAvailableSemaphores_.clear();
        swapChain_.reset();
    }

    // Surface can be destroyed without the device
    surface_.reset();

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

void Window::cleanupSwapChain() {
    swapChain_.reset();
}

void Window::createSurface() {
    surface_ = instance_->createSurface(window_);
}

void Window::createSwapChain() {
    swapChain_ = SwapChain::create(device_, *surface_)
        .vsync(config_.vsync)
        .imageCount(config_.framesInFlight + 1)
        .build();
}

void Window::createSyncObjects() {
    imageAvailableSemaphores_.resize(config_.framesInFlight);
    renderFinishedSemaphores_.resize(config_.framesInFlight);
    inFlightFences_.resize(config_.framesInFlight);

    for (uint32_t i = 0; i < config_.framesInFlight; i++) {
        imageAvailableSemaphores_[i] = std::make_unique<Semaphore>(device_);
        renderFinishedSemaphores_[i] = std::make_unique<Semaphore>(device_);
        inFlightFences_[i] = std::make_unique<Fence>(device_, true);  // Start signaled
    }
}

void Window::recreateSwapChain() {
    // Wait for device idle before recreating
    device_->waitIdle();

    // Get new size
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);

    // Wait if minimized
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }

    // Recreate swap chain
    swapChain_->recreate(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    FINEVK_INFO(LogCategory::Core, "Swap chain recreated: " + std::to_string(width) + "x" + std::to_string(height));
}

void Window::setupCallbacks() {
    glfwSetKeyCallback(window_, glfwKeyCallback);
    glfwSetMouseButtonCallback(window_, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(window_, glfwCursorPosCallback);
    glfwSetScrollCallback(window_, glfwScrollCallback);
    glfwSetCharCallback(window_, glfwCharCallback);
    glfwSetFramebufferSizeCallback(window_, glfwFramebufferSizeCallback);
}

// ============================================================================
// Window state
// ============================================================================

bool Window::isOpen() const {
    return window_ && !glfwWindowShouldClose(window_);
}

void Window::close() {
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

glm::uvec2 Window::size() const {
    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

uint32_t Window::width() const {
    return size().x;
}

uint32_t Window::height() const {
    return size().y;
}

void Window::setTitle(std::string_view title) {
    config_.title = std::string(title);
    glfwSetWindowTitle(window_, config_.title.c_str());
}

bool Window::isMinimized() const {
    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    return w == 0 || h == 0;
}

bool Window::isFocused() const {
    return glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
}

VkExtent2D Window::extent() const {
    if (swapChain_) {
        return swapChain_->extent();
    }
    auto s = size();
    return {s.x, s.y};
}

VkFormat Window::format() const {
    if (swapChain_) {
        return swapChain_->format().format;
    }
    return VK_FORMAT_B8G8R8A8_SRGB;  // Default assumption
}

// ============================================================================
// Device binding
// ============================================================================

void Window::bindDevice(LogicalDevice& device) {
    if (device_) {
        throw std::runtime_error("Window already bound to a device");
    }

    device_ = &device;

    // Register for device destruction notification so we can clean up
    // our resources before the device is destroyed
    deviceDestructionCallbackId_ = device_->onDestruction(
        [this](LogicalDevice*) {
            // Don't call releaseDeviceResources() here because the device
            // is about to call waitIdle() itself. Just clean up our handles.
            inFlightFences_.clear();
            renderFinishedSemaphores_.clear();
            imageAvailableSemaphores_.clear();
            swapChain_.reset();
            device_ = nullptr;
            deviceDestructionCallbackId_ = 0;
            FINEVK_DEBUG(LogCategory::Core, "Window device resources released (device destroying)");
        });

    createSwapChain();
    createSyncObjects();

    FINEVK_INFO(LogCategory::Core, "Window bound to device");
}

// ============================================================================
// Frame lifecycle
// ============================================================================

std::optional<FrameInfo> Window::beginFrame() {
    if (!device_) {
        throw std::runtime_error("Window not bound to a device. Call bindDevice() first.");
    }

    // Handle minimized window
    if (isMinimized()) {
        return std::nullopt;
    }

    // Wait for this frame's fence
    inFlightFences_[currentFrameIndex_]->wait();

    // Get sync objects for this frame
    VkSemaphore imageAvailable = imageAvailableSemaphores_[currentFrameIndex_]->handle();

    // Acquire next image
    auto result = swapChain_->acquireNextImage(imageAvailable);

    if (result.outOfDate) {
        recreateSwapChain();
        return std::nullopt;
    }

    // Reset fence only if we're going to submit work
    inFlightFences_[currentFrameIndex_]->reset();

    currentImageIndex_ = result.imageIndex;

    FrameInfo info;
    info.imageIndex = result.imageIndex;
    info.frameIndex = currentFrameIndex_;
    info.extent = swapChain_->extent();
    info.image = swapChain_->images()[result.imageIndex];
    info.imageView = swapChain_->imageViews()[result.imageIndex]->handle();
    info.imageAvailable = imageAvailable;
    info.renderFinished = renderFinishedSemaphores_[currentFrameIndex_]->handle();
    info.inFlightFence = inFlightFences_[currentFrameIndex_]->handle();

    return info;
}

bool Window::endFrame() {
    if (!device_) {
        throw std::runtime_error("Window not bound to a device");
    }

    VkSemaphore renderFinished = renderFinishedSemaphores_[currentFrameIndex_]->handle();
    auto result = swapChain_->present(device_->presentQueue(), currentImageIndex_, renderFinished);

    // Advance frame index
    currentFrameIndex_ = (currentFrameIndex_ + 1) % config_.framesInFlight;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
        return false;
    }

    return result == VK_SUCCESS;
}

void Window::waitIdle() {
    if (device_) {
        device_->waitIdle();
    }
}

// ============================================================================
// Event handling
// ============================================================================

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::waitEvents() {
    glfwWaitEvents();
}

bool Window::isKeyPressed(Key key) const {
    return key != GLFW_KEY_UNKNOWN && glfwGetKey(window_, key) == GLFW_PRESS;
}

bool Window::isKeyReleased(Key key) const {
    return key != GLFW_KEY_UNKNOWN && glfwGetKey(window_, key) == GLFW_RELEASE;
}

bool Window::isMouseButtonPressed(MouseButton button) const {
    return glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

glm::dvec2 Window::mousePosition() const {
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    return {x, y};
}

void Window::setMouseCaptured(bool captured) {
    glfwSetInputMode(window_, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

bool Window::isMouseCaptured() const {
    return glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
}

// ============================================================================
// GLFW callbacks
// ============================================================================

void Window::glfwKeyCallback(GLFWwindow* glfwWindow, int key, int /*scancode*/, int action, int mods) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window && window->keyCallback_) {
        window->keyCallback_(key,
                            action == GLFW_RELEASE ? Action::Release :
                            action == GLFW_PRESS ? Action::Press : Action::Repeat,
                            glfwModsToModifier(mods));
    }
}

void Window::glfwMouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window && window->mouseButtonCallback_) {
        window->mouseButtonCallback_(button,
                                     action == GLFW_RELEASE ? Action::Release : Action::Press,
                                     glfwModsToModifier(mods));
    }
}

void Window::glfwCursorPosCallback(GLFWwindow* glfwWindow, double x, double y) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window && window->mouseMoveCallback_) {
        window->mouseMoveCallback_(x, y);
    }
}

void Window::glfwScrollCallback(GLFWwindow* glfwWindow, double xoffset, double yoffset) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window && window->scrollCallback_) {
        window->scrollCallback_(xoffset, yoffset);
    }
}

void Window::glfwCharCallback(GLFWwindow* glfwWindow, unsigned int codepoint) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window && window->charCallback_) {
        window->charCallback_(codepoint);
    }
}

void Window::glfwFramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window) {
        window->framebufferResized_ = true;
        if (window->resizeCallback_) {
            window->resizeCallback_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
    }
}

// ============================================================================
// Modifier conversion (Key and MouseButton use GLFW constants directly)
// ============================================================================

Modifier Window::glfwModsToModifier(int glfwMods) {
    Modifier mods = Modifier::None;
    if (glfwMods & GLFW_MOD_SHIFT) mods = mods | Modifier::Shift;
    if (glfwMods & GLFW_MOD_CONTROL) mods = mods | Modifier::Control;
    if (glfwMods & GLFW_MOD_ALT) mods = mods | Modifier::Alt;
    if (glfwMods & GLFW_MOD_SUPER) mods = mods | Modifier::Super;
    if (glfwMods & GLFW_MOD_CAPS_LOCK) mods = mods | Modifier::CapsLock;
    if (glfwMods & GLFW_MOD_NUM_LOCK) mods = mods | Modifier::NumLock;
    return mods;
}

} // namespace finevk
