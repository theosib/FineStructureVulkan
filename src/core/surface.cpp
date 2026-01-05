#include "finevk/core/surface.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

Surface::Surface(Instance* instance, VkSurfaceKHR surface)
    : surface_(surface), instance_(instance) {
}

Surface::~Surface() {
    cleanup();
}

Surface::Surface(Surface&& other) noexcept
    : surface_(other.surface_), instance_(other.instance_) {
    other.surface_ = VK_NULL_HANDLE;
    other.instance_ = nullptr;
}

Surface& Surface::operator=(Surface&& other) noexcept {
    if (this != &other) {
        cleanup();
        surface_ = other.surface_;
        instance_ = other.instance_;
        other.surface_ = VK_NULL_HANDLE;
        other.instance_ = nullptr;
    }
    return *this;
}

void Surface::cleanup() {
    if (surface_ != VK_NULL_HANDLE && instance_ != nullptr) {
        vkDestroySurfaceKHR(instance_->handle(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        FINEVK_DEBUG(LogCategory::Core, "Surface destroyed");
    }
}

} // namespace finevk
