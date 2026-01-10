#include "finevk/engine/render_agent.hpp"
#include "finevk/core/logging.hpp"
#include <algorithm>

namespace finevk {

// =============================================================================
// Geometry Management
// =============================================================================

void RenderAgent::add(const Renderable& renderable) {
    renderables_.push_back(renderable);
    needsRecompute_ = true;
}

void RenderAgent::clear() {
    renderables_.clear();
    opaqueVisible_.clear();
    transparentSorted_.clear();
    needsRecompute_ = true;
}

// =============================================================================
// Camera Update
// =============================================================================

void RenderAgent::updateCamera(const CameraState& cameraState) {
    cameraState_ = &cameraState;

    // Check if camera moved significantly (triggers transparent resort)
    float distanceMoved = glm::distance(cameraState.position, lastCameraPos_);
    if (distanceMoved > 0.01f) {  // Threshold to avoid excessive resorting
        needsRecompute_ = true;
        lastCameraPos_ = cameraState.position;
    }
}

// =============================================================================
// Rendering Phases
// =============================================================================

void RenderAgent::renderOpaque(CommandBuffer& cmd) {
    if (!cameraState_) {
        FINEVK_WARN(LogCategory::Core, "RenderAgent: No camera set, skipping opaque render");
        return;
    }

    // Ensure culling/sorting is up to date
    if (needsRecompute_) {
        cullAndSort();
    }

    // Render all visible opaque objects
    for (const auto* renderable : opaqueVisible_) {
        renderOne(cmd, *renderable);
    }
}

void RenderAgent::renderTransparent(CommandBuffer& cmd) {
    if (!cameraState_) {
        FINEVK_WARN(LogCategory::Core, "RenderAgent: No camera set, skipping transparent render");
        return;
    }

    // Ensure culling/sorting is up to date
    if (needsRecompute_) {
        cullAndSort();
    }

    // Render all visible transparent objects (already sorted back-to-front)
    for (const auto* renderable : transparentSorted_) {
        renderOne(cmd, *renderable);
    }
}

void RenderAgent::renderUI(CommandBuffer& /* cmd */) {
    // Default: Do nothing
    // Override in derived class to add UI rendering
}

void RenderAgent::render(CommandBuffer& cmd) {
    renderOpaque(cmd);
    renderTransparent(cmd);
    renderUI(cmd);
}

// =============================================================================
// Culling and Sorting
// =============================================================================

void RenderAgent::cullAndSort() {
    opaqueVisible_.clear();
    transparentSorted_.clear();

    if (!cameraState_) {
        needsRecompute_ = false;
        return;
    }

    // Separate opaque and transparent, apply frustum culling
    for (const auto& renderable : renderables_) {
        // Frustum culling (if enabled)
        if (frustumCullingEnabled_) {
            AABB worldBounds = renderable.worldBounds();
            if (!worldBounds.intersectsFrustum(cameraState_->frustumPlanes)) {
                continue;  // Culled
            }
        }

        // Separate by transparency
        if (renderable.isTransparent) {
            transparentSorted_.push_back(&renderable);
        } else {
            opaqueVisible_.push_back(&renderable);
        }
    }

    // Sort transparent objects back-to-front (far to near)
    std::sort(transparentSorted_.begin(), transparentSorted_.end(),
        [cameraPos = cameraState_->position](const Renderable* a, const Renderable* b) {
            float distA = a->distanceToCamera(cameraPos);
            float distB = b->distanceToCamera(cameraPos);
            return distA > distB;  // Farther objects first
        });

    needsRecompute_ = false;
}

// =============================================================================
// Rendering Helper
// =============================================================================

void RenderAgent::renderOne(CommandBuffer& cmd, const Renderable& renderable) {
    if (!renderable.mesh) {
        return;  // Skip invalid renderables
    }

    // Bind pipeline if provided
    if (renderable.pipeline) {
        VkCommandBuffer vkCmd = cmd.handle();
        renderable.pipeline->bind(vkCmd);
    }

    // Bind material descriptors if provided
    if (renderable.material && renderable.pipelineLayout) {
        renderable.material->bind(cmd, renderable.pipelineLayout->handle());
    }

    // Push transform as push constant
    // Note: This assumes the pipeline uses push constants for MVP
    // In practice, you might want to update a uniform buffer instead
    // TODO: Define standard push constant layout or uniform buffer update pattern
    // For now, this is left to the user's shader/pipeline configuration

    // Bind and draw mesh
    renderable.mesh->bind(cmd);
    renderable.mesh->draw(cmd);
}

} // namespace finevk
