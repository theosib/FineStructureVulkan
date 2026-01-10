#pragma once

/**
 * @file finevk_engine.hpp
 * @brief Main header for finevk-engine library
 *
 * Game engine utilities built on top of finevk-core.
 * See docs/ENGINE_ARCHITECTURE.md for architecture and design philosophy.
 */

#include "finevk/finevk.hpp"
#include "finevk/engine/frame_clock.hpp"
#include "finevk/engine/deferred_disposer.hpp"
#include "finevk/engine/game_loop.hpp"

namespace finevk {

// Additional engine features will be added here as they are implemented:
// - RenderAgent system
// - WorkQueue / UploadQueue
// - DrawBatcher
// - CoordinateSystem
// - etc.

} // namespace finevk
