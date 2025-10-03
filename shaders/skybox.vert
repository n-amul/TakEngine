#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 texCoords;

void main() {
    texCoords = inPosition;
    
    // Remove translation from view matrix and apply to position
    vec4 pos = ubo.proj * ubo.view * vec4(inPosition, 1.0);
    
    // Set z to w to ensure the skybox is always at max depth
    // gl_Position = pos.xyww;
    gl_Position = vec4(pos.xy, pos.w, pos.w);
}