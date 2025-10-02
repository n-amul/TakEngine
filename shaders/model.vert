#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in uvec4 inJoint0;
layout(location = 5) in vec4 inWeight0;
layout(location = 6) in vec4 inColor;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV0;
layout(location = 3) out vec2 fragUV1;
layout(location = 4) out vec4 fragColor;
layout(location = 5) out vec3 fragViewPos;
layout(location = 6) out vec3 fragLightPos;
layout(location = 7) out flat uint fragMaterialIndex;

// Uniform buffer
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 lightPos;
    vec3 viewPos;
} ubo;

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialIndex;
} pushConstants;

void main() {
    // Use push constant model matrix for per-object transform
    mat4 worldMatrix = pushConstants.model;
    
    // Transform vertex position to world space
    vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (using normal matrix)
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);
    
    // Pass through texture coordinates
    fragUV0 = inUV0;
    fragUV1 = inUV1;
    
    // Pass through vertex color
    fragColor = inColor;
    
    // Pass light and view positions from UBO
    fragLightPos = ubo.lightPos;
    fragViewPos = ubo.viewPos;
    
    // Pass material index
    fragMaterialIndex = pushConstants.materialIndex;
    
    // Final vertex position
    gl_Position = ubo.proj * ubo.view * worldPos;
}