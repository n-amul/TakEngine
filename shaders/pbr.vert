#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in uvec4 inJoint0;
layout(location = 5) in vec4 inWeight0;
layout(location = 6) in vec4 inColor0;
layout(location = 7) in vec4 inTangent;


// Uniform buffers
layout(set = 0, binding = 0) uniform UBO {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
} ubo;

#define MAX_NUM_JOINTS 128

struct MeshDataBlock {
    mat4 matrix;
    mat4 jointMatrix[MAX_NUM_JOINTS];
    uint jointCount;
};

layout(set = 2, binding = 0) readonly buffer SSBO {
    MeshDataBlock meshData[];
};

layout (push_constant) uniform PushConstants {
	int meshIndex;
	int materialIndex;
} pushConstants;

// Outputs to fragment shader
layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV0;
layout (location = 3) out vec2 outUV1;
layout (location = 4) out vec4 outColor0;
layout(location = 5) out vec4 outTangent;


void main() {
    outColor0 = inColor0;

    vec4 locPos;
    if (meshData[pushConstants.meshIndex].jointCount > 0) {
        // Mesh is skinned
        mat4 skinMat = 
            inWeight0.x * meshData[pushConstants.meshIndex].jointMatrix[inJoint0.x] +
            inWeight0.y * meshData[pushConstants.meshIndex].jointMatrix[inJoint0.y] +
            inWeight0.z * meshData[pushConstants.meshIndex].jointMatrix[inJoint0.z] +
            inWeight0.w * meshData[pushConstants.meshIndex].jointMatrix[inJoint0.w];
        locPos = ubo.model * meshData[pushConstants.meshIndex].matrix * skinMat * vec4(inPos, 1.0);
        outNormal = normalize(transpose(inverse(mat3(ubo.model * meshData[pushConstants.meshIndex].matrix * skinMat))) * inNormal);
    } else {
        locPos = ubo.model * meshData[pushConstants.meshIndex].matrix * vec4(inPos, 1.0);
        outNormal = normalize(transpose(inverse(mat3(ubo.model * meshData[pushConstants.meshIndex].matrix))) * inNormal);
    }
    // yflip for vulkan
    locPos.y = -locPos.y;
    outWorldPos = locPos.xyz / locPos.w;
    outUV0 = inUV0;
    outUV1 = inUV1;
    outTangent = inTangent;
    gl_Position = ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
}

