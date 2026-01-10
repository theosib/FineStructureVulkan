#include "finevk/engine/camera.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace finevk {

// =============================================================================
// AABB Implementation
// =============================================================================

bool AABB::intersectsFrustum(const std::array<glm::vec4, 6>& frustumPlanes) const {
    // Test AABB against each frustum plane
    // If AABB is completely outside any plane, it's culled
    for (const auto& plane : frustumPlanes) {
        // Find the positive vertex (farthest point in plane normal direction)
        glm::vec3 positiveVertex = min;
        if (plane.x >= 0) positiveVertex.x = max.x;
        if (plane.y >= 0) positiveVertex.y = max.y;
        if (plane.z >= 0) positiveVertex.z = max.z;

        // Check if positive vertex is outside (negative side of plane)
        float distance = glm::dot(glm::vec3(plane), positiveVertex) + plane.w;
        if (distance < 0) {
            return false;  // AABB is completely outside this plane
        }
    }

    return true;  // AABB intersects or is inside frustum
}

AABB AABB::transform(const glm::mat4& matrix) const {
    // Transform all 8 corners and recompute axis-aligned bounds
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {min.x, max.y, min.z},
        {max.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {min.x, max.y, max.z},
        {max.x, max.y, max.z}
    };

    glm::vec3 newMin(FLT_MAX);
    glm::vec3 newMax(-FLT_MAX);

    for (const auto& corner : corners) {
        glm::vec3 transformed = glm::vec3(matrix * glm::vec4(corner, 1.0f));
        newMin = glm::min(newMin, transformed);
        newMax = glm::max(newMax, transformed);
    }

    return AABB{newMin, newMax};
}

AABB AABB::fromCenterExtents(const glm::vec3& center, const glm::vec3& extents) {
    return AABB{center - extents, center + extents};
}

AABB AABB::fromMinMax(const glm::vec3& min, const glm::vec3& max) {
    return AABB{min, max};
}

// =============================================================================
// Camera Implementation
// =============================================================================

Camera::Camera() {
    updateState();
}

// =============================================================================
// Projection Configuration
// =============================================================================

void Camera::setPerspective(float fovDegrees, float aspect, float nearPlane, float farPlane) {
    isPerspective_ = true;
    fov_ = fovDegrees;
    aspect_ = aspect;
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;

    state_.projection = glm::perspective(
        glm::radians(fovDegrees),
        aspect,
        nearPlane,
        farPlane
    );

    // Vulkan NDC: Y is flipped compared to OpenGL
    state_.projection[1][1] *= -1;
}

void Camera::setOrthographic(float left, float right, float bottom, float top,
                            float nearPlane, float farPlane) {
    isPerspective_ = false;
    orthoLeft_ = left;
    orthoRight_ = right;
    orthoBottom_ = bottom;
    orthoTop_ = top;
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;

    state_.projection = glm::ortho(left, right, bottom, top, nearPlane, farPlane);

    // Vulkan depth range is [0, 1]
    state_.projection[2][2] *= -1;
}

// =============================================================================
// Position Control
// =============================================================================

void Camera::move(const glm::vec3& delta) {
    position_ += delta;
}

void Camera::moveTo(const glm::vec3& position) {
    position_ = position;
}

void Camera::moveForward(float distance) {
    position_ += forward_ * distance;
}

void Camera::moveRight(float distance) {
    position_ += glm::cross(forward_, up_) * distance;
}

void Camera::moveUp(float distance) {
    position_ += up_ * distance;
}

// =============================================================================
// Orientation Control
// =============================================================================

void Camera::rotate(float pitch, float yaw, float roll) {
    // Build rotation quaternion
    glm::quat qPitch = glm::angleAxis(glm::radians(pitch), right());
    glm::quat qYaw = glm::angleAxis(glm::radians(yaw), up_);
    glm::quat qRoll = glm::angleAxis(glm::radians(roll), forward_);

    glm::quat rotation = qYaw * qPitch * qRoll;

    // Apply to forward and up vectors
    forward_ = glm::normalize(rotation * forward_);
    up_ = glm::normalize(rotation * up_);
}

void Camera::rotatePitch(float degrees) {
    rotate(degrees, 0.0f, 0.0f);
}

void Camera::rotateYaw(float degrees) {
    rotate(0.0f, degrees, 0.0f);
}

void Camera::rotateRoll(float degrees) {
    rotate(0.0f, 0.0f, degrees);
}

void Camera::lookAt(const glm::vec3& target, const glm::vec3& worldUp) {
    forward_ = glm::normalize(target - position_);

    // Recompute right and up vectors
    glm::vec3 right = glm::normalize(glm::cross(forward_, worldUp));
    up_ = glm::normalize(glm::cross(right, forward_));
}

void Camera::setOrientation(const glm::vec3& forward, const glm::vec3& up) {
    forward_ = glm::normalize(forward);
    up_ = glm::normalize(up);

    // Ensure orthogonality
    glm::vec3 right = glm::cross(forward_, up_);
    up_ = glm::cross(right, forward_);
}

// =============================================================================
// State Update
// =============================================================================

void Camera::updateState() {
    // Update position
    state_.position = position_;

    // Compute view matrix
    state_.view = glm::lookAt(position_, position_ + forward_, up_);

    // Compute view-projection matrix
    state_.viewProjection = state_.projection * state_.view;

    // Extract frustum planes
    extractFrustumPlanes();
}

void Camera::extractFrustumPlanes() {
    // Extract frustum planes from view-projection matrix
    // Planes are in format: Ax + By + Cz + D = 0
    // where (A, B, C) is the plane normal

    const glm::mat4& m = state_.viewProjection;

    // Left plane
    state_.frustumPlanes[0] = glm::vec4(
        m[0][3] + m[0][0],
        m[1][3] + m[1][0],
        m[2][3] + m[2][0],
        m[3][3] + m[3][0]
    );

    // Right plane
    state_.frustumPlanes[1] = glm::vec4(
        m[0][3] - m[0][0],
        m[1][3] - m[1][0],
        m[2][3] - m[2][0],
        m[3][3] - m[3][0]
    );

    // Bottom plane
    state_.frustumPlanes[2] = glm::vec4(
        m[0][3] + m[0][1],
        m[1][3] + m[1][1],
        m[2][3] + m[2][1],
        m[3][3] + m[3][1]
    );

    // Top plane
    state_.frustumPlanes[3] = glm::vec4(
        m[0][3] - m[0][1],
        m[1][3] - m[1][1],
        m[2][3] - m[2][1],
        m[3][3] - m[3][1]
    );

    // Near plane
    state_.frustumPlanes[4] = glm::vec4(
        m[0][3] + m[0][2],
        m[1][3] + m[1][2],
        m[2][3] + m[2][2],
        m[3][3] + m[3][2]
    );

    // Far plane
    state_.frustumPlanes[5] = glm::vec4(
        m[0][3] - m[0][2],
        m[1][3] - m[1][2],
        m[2][3] - m[2][2],
        m[3][3] - m[3][2]
    );

    // Normalize planes
    for (auto& plane : state_.frustumPlanes) {
        float length = glm::length(glm::vec3(plane));
        plane /= length;
    }
}

} // namespace finevk
