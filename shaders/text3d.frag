#version 450

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

// Texture sampler (font atlas)
layout(set = 0, binding = 1) uniform sampler2D texSampler;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Sample font atlas (alpha channel contains glyph) and multiply by vertex color
    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = texColor * fragColor;

    // Discard fully transparent pixels to avoid depth issues
    if (outColor.a < 0.01) {
        discard;
    }
}
