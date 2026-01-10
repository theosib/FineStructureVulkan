#pragma once

#include "finevk/engine/camera.hpp"
#include "finevk/high/mesh.hpp"
#include "finevk/high/material.hpp"
#include "finevk/rendering/pipeline.hpp"
#include "finevk/device/command.hpp"
#include <vector>
#include <memory>

namespace finevk {

/**
 * @brief Renderable object bundle
 *
 * Combines mesh, material, pipeline, transform, and bounding information for rendering.
 * Used by RenderAgent to organize and cull geometry.
 */
struct Renderable {
    Mesh* mesh = nullptr;
    Material* material = nullptr;
    GraphicsPipeline* pipeline = nullptr;
    PipelineLayout* pipelineLayout = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    AABB localBounds;  // Bounding box in local/model space
    bool isTransparent = false;

    /// Compute world-space AABB (applies transform to localBounds)
    AABB worldBounds() const {
        return localBounds.transform(transform);
    }

    /// Distance to camera position (for transparent sorting)
    float distanceToCamera(const glm::vec3& cameraPos) const {
        glm::vec3 objectCenter = glm::vec3(transform * glm::vec4(localBounds.center(), 1.0f));
        return glm::distance(cameraPos, objectCenter);
    }
};

/**
 * @brief Example rendering system with automatic culling and sorting
 *
 * RenderAgent demonstrates best practices for organizing rendering:
 * - Separates opaque and transparent geometry
 * - Performs frustum culling (optional)
 * - Sorts transparent objects back-to-front
 * - Provides phase-based rendering (opaque → transparent → UI)
 *
 * **Design Intent**: This is an example pattern, not a mandatory framework.
 * Users create and own RenderAgent in their game class. It's independent
 * of GameLoop - just a helper for organizing draw calls.
 *
 * Usage:
 * @code
 * class MyGame : public GameLoop {
 *     Camera camera_;
 *     RenderAgent renderAgent_;
 *
 * protected:
 *     void onFixedUpdate(float dt) override {
 *         // Game logic, maybe mark renderAgent dirty
 *     }
 *
 *     void onRender(float dt, float interpolation) override {
 *         camera_.updateState();
 *         renderAgent_.updateCamera(camera_.state());
 *
 *         auto cmd = acquireCommandBuffer();
 *         renderAgent_.render(*cmd);  // Or call phases individually
 *     }
 * };
 * @endcode
 */
class RenderAgent {
public:
    RenderAgent() = default;
    virtual ~RenderAgent() = default;

    // Non-copyable, movable
    RenderAgent(const RenderAgent&) = delete;
    RenderAgent& operator=(const RenderAgent&) = delete;
    RenderAgent(RenderAgent&&) noexcept = default;
    RenderAgent& operator=(RenderAgent&&) noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Enable/disable frustum culling (default: enabled)
    void setFrustumCullingEnabled(bool enabled) {
        if (frustumCullingEnabled_ != enabled) {
            frustumCullingEnabled_ = enabled;
            needsRecompute_ = true;
        }
    }

    bool isFrustumCullingEnabled() const { return frustumCullingEnabled_; }

    // =========================================================================
    // Geometry Management
    // =========================================================================

    /**
     * @brief Add a renderable object
     *
     * The Renderable is copied into internal storage. Mesh and Material
     * are non-owning pointers - caller must ensure they remain valid.
     */
    void add(const Renderable& renderable);

    /**
     * @brief Remove all renderables
     */
    void clear();

    /**
     * @brief Mark dirty (forces recompute on next render)
     *
     * Call when world geometry changes (objects added/removed/moved).
     */
    void markDirty() { needsRecompute_ = true; }

    // =========================================================================
    // Camera Update
    // =========================================================================

    /**
     * @brief Update camera state
     *
     * Stores reference to camera state and triggers transparent object
     * resorting if camera position changed significantly.
     *
     * Call before rendering each frame.
     */
    void updateCamera(const CameraState& cameraState);

    // =========================================================================
    // Rendering Phases
    // =========================================================================

    /**
     * @brief Render opaque geometry
     *
     * Renders all non-transparent objects that pass frustum culling.
     * Order is arbitrary (no sorting needed for opaque).
     */
    void renderOpaque(CommandBuffer& cmd);

    /**
     * @brief Render transparent geometry
     *
     * Renders transparent objects back-to-front (sorted by distance to camera).
     * Requires depth test but not depth write for correct blending.
     */
    void renderTransparent(CommandBuffer& cmd);

    /**
     * @brief Render UI overlay
     *
     * Virtual method for UI rendering. Default does nothing.
     * Override in derived class to add UI rendering.
     */
    virtual void renderUI(CommandBuffer& cmd);

    /**
     * @brief Render all phases in order
     *
     * Convenience method that calls:
     * 1. renderOpaque(cmd)
     * 2. renderTransparent(cmd)
     * 3. renderUI(cmd)
     */
    void render(CommandBuffer& cmd);

    // =========================================================================
    // Statistics
    // =========================================================================

    size_t totalObjects() const { return renderables_.size(); }
    size_t visibleObjects() const { return opaqueVisible_.size() + transparentSorted_.size(); }
    size_t culledObjects() const { return totalObjects() - visibleObjects(); }
    size_t opaqueCount() const { return opaqueVisible_.size(); }
    size_t transparentCount() const { return transparentSorted_.size(); }

protected:
    /**
     * @brief Perform culling and sorting
     *
     * Called automatically before rendering if needsRecompute_ is true.
     * - Frustum culls all objects (if enabled)
     * - Separates opaque and transparent
     * - Sorts transparent back-to-front
     */
    void cullAndSort();

    /**
     * @brief Render a single renderable
     *
     * Helper method for rendering. Binds material, pushes transform,
     * and issues draw call.
     */
    void renderOne(CommandBuffer& cmd, const Renderable& renderable);

private:
    // All renderables
    std::vector<Renderable> renderables_;

    // Culled and sorted views (pointers into renderables_)
    std::vector<const Renderable*> opaqueVisible_;
    std::vector<const Renderable*> transparentSorted_;

    // Camera state reference
    const CameraState* cameraState_ = nullptr;
    glm::vec3 lastCameraPos_{0.0f};

    // Flags
    bool frustumCullingEnabled_ = true;
    bool needsRecompute_ = true;
};

} // namespace finevk
