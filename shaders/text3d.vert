#version 450

// Vertex input - 3D position
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

// Uniform buffer: view-projection matrix
layout(set = 0, binding = 0) uniform TextUBO {
    mat4 viewProjection;
} ubo;

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    gl_Position = ubo.viewProjection * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
