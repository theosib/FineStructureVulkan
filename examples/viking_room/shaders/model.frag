#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    // Simple diffuse lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(fragNormal);
    float diff = max(dot(normal, lightDir), 0.0) * 0.7 + 0.3;  // Ambient = 0.3

    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = vec4(texColor.rgb * diff, texColor.a);
}
