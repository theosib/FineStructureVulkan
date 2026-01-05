#include "finevk/core/instance.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/logging.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace finevk {

// Validation layer name
static const char* const VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

// ============================================================================
// Builder implementation
// ============================================================================

Instance::Builder::Builder() {
#ifdef NDEBUG
    validationEnabled_ = false;
#else
    validationEnabled_ = true;
#endif
}

Instance::Builder& Instance::Builder::applicationName(std::string_view name) {
    appName_ = std::string(name);
    return *this;
}

Instance::Builder& Instance::Builder::applicationVersion(uint32_t major, uint32_t minor, uint32_t patch) {
    appVersion_ = VK_MAKE_VERSION(major, minor, patch);
    return *this;
}

Instance::Builder& Instance::Builder::engineName(std::string_view name) {
    engineName_ = std::string(name);
    return *this;
}

Instance::Builder& Instance::Builder::engineVersion(uint32_t major, uint32_t minor, uint32_t patch) {
    engineVersion_ = VK_MAKE_VERSION(major, minor, patch);
    return *this;
}

Instance::Builder& Instance::Builder::apiVersion(uint32_t version) {
    apiVersion_ = version;
    return *this;
}

Instance::Builder& Instance::Builder::enableValidation(bool enable) {
    validationEnabled_ = enable;
    return *this;
}

Instance::Builder& Instance::Builder::addExtension(const char* extension) {
    extensions_.push_back(extension);
    return *this;
}

Instance::Builder& Instance::Builder::addExtensions(const std::vector<const char*>& extensions) {
    extensions_.insert(extensions_.end(), extensions.begin(), extensions.end());
    return *this;
}

std::vector<const char*> Instance::Builder::getRequiredExtensions() const {
    // Get GLFW required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    // Add user-requested extensions
    extensions.insert(extensions.end(), extensions_.begin(), extensions_.end());

    // Add debug extension if validation is enabled
    if (validationEnabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // macOS/MoltenVK portability extension
#ifdef VK_USE_PLATFORM_MACOS_MVK
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back("VK_KHR_get_physical_device_properties2");
#endif

    return extensions;
}

bool Instance::Builder::checkValidationLayerSupport() const {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layerProperties : availableLayers) {
        if (std::strcmp(VALIDATION_LAYER, layerProperties.layerName) == 0) {
            return true;
        }
    }
    return false;
}

InstancePtr Instance::Builder::build() {
    // Initialize GLFW if not already done
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Check validation layer support
    if (validationEnabled_ && !checkValidationLayerSupport()) {
        FINEVK_WARN(LogCategory::Core, "Validation layers requested but not available");
        validationEnabled_ = false;
    }

    // Application info
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName_.c_str();
    appInfo.applicationVersion = appVersion_;
    appInfo.pEngineName = engineName_.c_str();
    appInfo.engineVersion = engineVersion_;
    appInfo.apiVersion = apiVersion_;

    // Get required extensions
    auto extensions = getRequiredExtensions();

    // Instance create info
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // macOS portability flag
#ifdef VK_USE_PLATFORM_MACOS_MVK
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    // Validation layers
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (validationEnabled_) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &VALIDATION_LAYER;

        // Enable debug messenger for instance creation/destruction
        DebugMessenger::populateCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;

        FINEVK_INFO(LogCategory::Core, "Validation layers enabled");
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    // Create the instance
    VkInstance vkInstance;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &vkInstance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    FINEVK_INFO(LogCategory::Core, "Vulkan instance created successfully");

    // Create our Instance wrapper
    auto instance = InstancePtr(new Instance());
    instance->instance_ = vkInstance;
    instance->validationEnabled_ = validationEnabled_;

    // Create debug messenger if validation is enabled
    if (validationEnabled_) {
        instance->debugMessenger_ = DebugMessenger::create(instance.get());
    }

    return instance;
}

// ============================================================================
// Instance implementation
// ============================================================================

Instance::Builder Instance::create() {
    return Builder();
}

Instance::~Instance() {
    cleanup();
}

Instance::Instance(Instance&& other) noexcept
    : instance_(other.instance_)
    , validationEnabled_(other.validationEnabled_)
    , debugMessenger_(std::move(other.debugMessenger_)) {
    other.instance_ = VK_NULL_HANDLE;
}

Instance& Instance::operator=(Instance&& other) noexcept {
    if (this != &other) {
        cleanup();
        instance_ = other.instance_;
        validationEnabled_ = other.validationEnabled_;
        debugMessenger_ = std::move(other.debugMessenger_);
        other.instance_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Instance::cleanup() {
    // Destroy debug messenger before instance
    debugMessenger_.reset();

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        FINEVK_DEBUG(LogCategory::Core, "Vulkan instance destroyed");
    }
}

SurfacePtr Instance::createSurface(GLFWwindow* window) {
    return Surface::fromGLFW(this, window);
}

// Note: enumeratePhysicalDevices() and selectPhysicalDevice() will be
// implemented in Layer 2 (src/device/physical_device.cpp) when the
// PhysicalDevice class is fully defined. They are declared in instance.hpp
// but compilation is deferred to avoid incomplete type errors.

} // namespace finevk
