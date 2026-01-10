#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>

namespace finevk {

/**
 * @brief Axis-aligned bounding box for culling
 */
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    /// Check if AABB intersects frustum (6 planes)
    bool intersectsFrustum(const std::array<glm::vec4, 6>& frustumPlanes) const;

    /// Transform AABB by matrix (creates new axis-aligned bounds)
    AABB transform(const glm::mat4& matrix) const;

    /// Get center point
    glm::vec3 center() const { return (min + max) * 0.5f; }

    /// Get extents (half-sizes)
    glm::vec3 extents() const { return (max - min) * 0.5f; }

    /// Create from center and extents
    static AABB fromCenterExtents(const glm::vec3& center, const glm::vec3& extents);

    /// Create from mesh bounds
    static AABB fromMinMax(const glm::vec3& min, const glm::vec3& max);
};

/**
 * @brief Minimal camera state for rendering
 *
 * Contains pre-computed matrices and frustum planes needed by render systems.
 * Extracted from Camera for efficient passing to RenderAgent.
 */
struct CameraState {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};

    /// Frustum planes in world space (left, right, bottom, top, near, far)
    /// Planes point inward (negative half-space is inside frustum)
    std::array<glm::vec4, 6> frustumPlanes;
};

/**
 * @brief Camera with movement and orientation helpers
 *
 * Provides a complete camera system with:
 * - Perspective and orthographic projections
 * - Movement helpers (move, moveForward, etc.)
 * - Orientation helpers (rotate, lookAt, etc.)
 * - Automatic frustum extraction for culling
 *
 * Usage:
 * @code
 * Camera camera;
 * camera.setPerspective(45.0f, aspectRatio, 0.1f, 100.0f);
 * camera.lookAt(glm::vec3(0, 5, 10), glm::vec3(0, 0, 0));
 *
 * // In game loop:
 * camera.moveForward(speed * dt);
 * camera.rotateYaw(mouseDeltaX * sensitivity);
 * camera.updateState();
 *
 * // Pass to render agent:
 * renderAgent.updateCamera(camera.state());
 * @endcode
 */
class Camera {
public:
    Camera();

    // =========================================================================
    // Projection Configuration
    // =========================================================================

    /**
     * @brief Set perspective projection
     *
     * @param fovDegrees Vertical field of view in degrees
     * @param aspect Aspect ratio (width / height)
     * @param nearPlane Near clipping plane
     * @param farPlane Far clipping plane
     */
    void setPerspective(float fovDegrees, float aspect, float nearPlane, float farPlane);

    /**
     * @brief Set orthographic projection
     *
     * @param left Left clipping plane
     * @param right Right clipping plane
     * @param bottom Bottom clipping plane
     * @param top Top clipping plane
     * @param nearPlane Near clipping plane
     * @param farPlane Far clipping plane
     */
    void setOrthographic(float left, float right, float bottom, float top,
                        float nearPlane, float farPlane);

    // =========================================================================
    // Position Control
    // =========================================================================

    /// Move by delta in world space
    void move(const glm::vec3& delta);

    /// Set absolute position in world space
    void moveTo(const glm::vec3& position);

    /// Move forward along camera's forward vector
    void moveForward(float distance);

    /// Move backward along camera's forward vector
    void moveBackward(float distance) { moveForward(-distance); }

    /// Move right along camera's right vector
    void moveRight(float distance);

    /// Move left along camera's right vector
    void moveLeft(float distance) { moveRight(-distance); }

    /// Move up along camera's up vector
    void moveUp(float distance);

    /// Move down along camera's up vector
    void moveDown(float distance) { moveUp(-distance); }

    // =========================================================================
    // Orientation Control
    // =========================================================================

    /**
     * @brief Rotate camera by pitch, yaw, roll (in degrees)
     *
     * @param pitch Rotation around right axis (up/down)
     * @param yaw Rotation around up axis (left/right)
     * @param roll Rotation around forward axis (tilt)
     */
    void rotate(float pitch, float yaw, float roll = 0.0f);

    /// Rotate around right axis (look up/down) in degrees
    void rotatePitch(float degrees);

    /// Rotate around up axis (look left/right) in degrees
    void rotateYaw(float degrees);

    /// Rotate around forward axis (tilt) in degrees
    void rotateRoll(float degrees);

    /**
     * @brief Point camera at target position
     *
     * @param target World position to look at
     * @param worldUp Up vector in world space (default: Y-up)
     */
    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0, 1, 0));

    /// Set orientation from forward and up vectors
    void setOrientation(const glm::vec3& forward, const glm::vec3& up);

    // =========================================================================
    // State Access
    // =========================================================================

    /**
     * @brief Update camera state (matrices and frustum)
     *
     * Call after any position/orientation changes and before using state().
     * Recomputes view matrix, view-projection matrix, and extracts frustum planes.
     */
    void updateState();

    /// Get current camera state (call updateState() first if camera moved)
    const CameraState& state() const { return state_; }

    /// Get current position
    const glm::vec3& position() const { return position_; }

    /// Get current forward vector (direction camera is facing)
    const glm::vec3& forward() const { return forward_; }

    /// Get current up vector
    const glm::vec3& up() const { return up_; }

    /// Get current right vector
    glm::vec3 right() const { return glm::cross(forward_, up_); }

private:
    void extractFrustumPlanes();

    // Position and orientation
    glm::vec3 position_{0.0f, 0.0f, 0.0f};
    glm::vec3 forward_{0.0f, 0.0f, -1.0f};  // -Z in OpenGL/Vulkan
    glm::vec3 up_{0.0f, 1.0f, 0.0f};

    // Projection parameters (for rebuild if aspect changes)
    bool isPerspective_ = true;
    float fov_ = 45.0f;
    float aspect_ = 1.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 100.0f;

    // Orthographic parameters
    float orthoLeft_ = -1.0f;
    float orthoRight_ = 1.0f;
    float orthoBottom_ = -1.0f;
    float orthoTop_ = 1.0f;

    // Computed state
    CameraState state_;
};

} // namespace finevk
