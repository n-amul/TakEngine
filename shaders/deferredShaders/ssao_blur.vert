#version 450

// FIX: This file was empty (only #version 450) — would fail SPIR-V compilation.
// Fullscreen quad vertex shader for SSAO blur pass.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = inTexCoord;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
