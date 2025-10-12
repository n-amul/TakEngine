#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Vertex attributes (from tak::Vertex)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in uvec4 inJoint0;
layout(location = 5) in vec4 inWeight0;
layout(location = 6) in vec4 inColor0;
layout(location = 7) in vec4 inTangent;

// Uniform buffers
layout(set = 0, binding = 0) uniform UBOMatrices {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
} ubo;

// Mesh data SSBO - using single struct to avoid dynamic indexing issues
struct MeshData {
    mat4 matrix;
    mat4 jointMatrix[64]; // MAX_NUM_JOINTS
    uint jointcount;
    uint padding[3];
};

layout(set = 2, binding = 0) readonly buffer MeshDataBuffer {
    MeshData meshData[];
} meshBuffer;

// Push constants
layout(push_constant) uniform PushConsts {
    int meshIndex;
    int materialIndex;
} pushConsts;

// Outputs to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV0;
layout(location = 3) out vec2 outUV1;
layout(location = 4) out vec4 outColor;
layout(location = 5) out vec4 outTangent;

void main() {
    vec4 locPos;
    
    // Access mesh data using push constant index
    int idx = pushConsts.meshIndex;
    mat4 meshMatrix = meshBuffer.meshData[idx].matrix;
    uint jointCount = meshBuffer.meshData[idx].jointcount;
    
    // Skinning
    if (jointCount > 0u) {
        // Calculate skinned position
        mat4 skinMat = 
            inWeight0.x * meshBuffer.meshData[idx].jointMatrix[int(inJoint0.x)] +
            inWeight0.y * meshBuffer.meshData[idx].jointMatrix[int(inJoint0.y)] +
            inWeight0.z * meshBuffer.meshData[idx].jointMatrix[int(inJoint0.z)] +
            inWeight0.w * meshBuffer.meshData[idx].jointMatrix[int(inJoint0.w)];
            
        locPos = meshMatrix * skinMat * vec4(inPos, 1.0);
        
        // Transform normal with skinning
        mat3 normalMatrix = transpose(inverse(mat3(ubo.model * meshMatrix * skinMat)));
        outNormal = normalize(normalMatrix * inNormal);
        
        // Transform tangent
        outTangent = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
    } else {
        // No skinning
        locPos = meshMatrix * vec4(inPos, 1.0);
        
        mat3 normalMatrix = transpose(inverse(mat3(ubo.model * meshMatrix)));
        outNormal = normalize(normalMatrix * inNormal);
        outTangent = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
    }
    
    // World space position
    outWorldPos = vec3(ubo.model * locPos);
    
    // Pass through texture coordinates and vertex color
    outUV0 = inUV0;
    outUV1 = inUV1;
    outColor = inColor0;
    
    // Final position
    gl_Position = ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
}