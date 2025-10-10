#version 450

// Vertex attributes
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in uvec4 inJoint0;
layout(location = 5) in vec4 inWeight0;
layout(location = 6) in vec4 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV0;
layout(location = 3) out vec2 outUV1;
layout(location = 4) out vec4 outColor;
layout(location = 5) out vec3 outViewDir; //consider compute formula use these in cpu and pass the result instead of using outviewDir and LightDir
layout(location = 6) out vec3 outLightDir;

// Set 0: Scene uniforms
layout(set = 0, binding = 0) uniform UBOMatrices {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
} matrices;

// Set 3: Mesh data SSBO
layout(std430, set = 3, binding = 0) readonly buffer MeshDataBuffer {
    mat4 meshMatrices[];
    // Note: In actual implementation, this would be a struct array with matrix and joint data
    // But for simplicity, we'll use push constants to index into it
} meshData;

// Push constants
layout(push_constant) uniform PushConstants {
    int meshIndex;
    int materialIndex;
} pushConstants;

// Constants
#define MAX_NUM_JOINTS 128

void main() {
    vec4 localPos = vec4(inPos, 1.0);
    vec4 localNormal = vec4(inNormal, 0.0);
    
    // Get the mesh's world transform
    mat4 worldMatrix = meshData.meshMatrices[pushConstants.meshIndex];
    
    // Apply skinning if weights are present
    if (inWeight0.x + inWeight0.y + inWeight0.z + inWeight0.w > 0.0) {
        // Skinning would be applied here using joint matrices
        // For now, we'll just use the base position
    }
    
    // Transform position to world space
    vec4 worldPos = worldMatrix * localPos;
    outWorldPos = worldPos.xyz;
    
    // Transform normal to world space (using normal matrix)
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    outNormal = normalize(normalMatrix * localNormal.xyz);
    
    // Pass through texture coordinates and color
    outUV0 = inUV0;
    outUV1 = inUV1;
    outColor = inColor;
    
    // Calculate view direction (from surface to camera)
    outViewDir = normalize(matrices.camPos - outWorldPos);
    
    // Light direction is passed from uniform buffer
    // This will be used in fragment shader
    outLightDir = vec3(0.0, 0.0, 1.0); // Default light direction
    
    // Final position
    gl_Position = matrices.projection * matrices.view * worldPos;
}