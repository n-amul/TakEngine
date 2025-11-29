#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniforms
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    fragWorldPos = vec3(ubo.model * vec4(inPosition, 1.0));
    fragNormal = mat3(ubo.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    
    gl_Position = ubo.proj * ubo.view * vec4(fragWorldPos, 1.0);
}