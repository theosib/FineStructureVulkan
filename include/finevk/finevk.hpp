#pragma once

// FineStructure Vulkan - Main include file
// Include this header to access all of finevk-core

// Core foundation (Layer 1)
#include "finevk/core/logging.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/debug.hpp"

// Device & Memory Management (Layer 2)
#include "finevk/device/physical_device.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/memory.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/image.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/device/command.hpp"

// Rendering Infrastructure (Layer 3)
#include "finevk/rendering/swapchain.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/framebuffer.hpp"
#include "finevk/rendering/pipeline.hpp"
#include "finevk/rendering/sync.hpp"
#include "finevk/rendering/descriptors.hpp"

// Forward declarations and common types
#include "finevk/core/types.hpp"
