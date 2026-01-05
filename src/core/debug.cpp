#include "finevk/core/debug.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// Debug callback function
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
    Logger::global().vulkanMessage(messageSeverity, messageType, pCallbackData);
    return VK_FALSE; // Don't abort the call that triggered validation
}

void DebugMessenger::populateCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;
}

DebugMessengerPtr DebugMessenger::create(Instance* instance) {
    if (!instance) {
        throw std::runtime_error("DebugMessenger::create: instance is null");
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateCreateInfo(createInfo);

    VkDebugUtilsMessengerEXT messenger;
    VkResult result = createDebugMessenger(
        instance->handle(), &createInfo, nullptr, &messenger);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create debug messenger");
    }

    return DebugMessengerPtr(new DebugMessenger(instance, messenger));
}

DebugMessenger::DebugMessenger(Instance* instance, VkDebugUtilsMessengerEXT messenger)
    : messenger_(messenger), instance_(instance) {
}

DebugMessenger::~DebugMessenger() {
    cleanup();
}

DebugMessenger::DebugMessenger(DebugMessenger&& other) noexcept
    : messenger_(other.messenger_), instance_(other.instance_) {
    other.messenger_ = VK_NULL_HANDLE;
    other.instance_ = nullptr;
}

DebugMessenger& DebugMessenger::operator=(DebugMessenger&& other) noexcept {
    if (this != &other) {
        cleanup();
        messenger_ = other.messenger_;
        instance_ = other.instance_;
        other.messenger_ = VK_NULL_HANDLE;
        other.instance_ = nullptr;
    }
    return *this;
}

void DebugMessenger::cleanup() {
    if (messenger_ != VK_NULL_HANDLE && instance_ != nullptr) {
        destroyDebugMessenger(instance_->handle(), messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
}

// Extension function loaders
VkResult createDebugMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        func(instance, messenger, pAllocator);
    }
}

} // namespace finevk
