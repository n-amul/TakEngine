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
    mat4 normalMatrix; //transpose(inverse(view*model)) : moves normals to view space
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormalView; //viewspace normal
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPosView; //viewspace position

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition,1.0);

    vec4 viewPos = ubo.view * worldPos;
    fragPosView = viewPos.xyz;

    fragNormalView = mat3(ubo.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    
    gl_Position = ubo.proj * viewPos;
}