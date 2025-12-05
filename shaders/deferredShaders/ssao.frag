#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outSSAO;

// G-Buffer inputs
layout(binding = 0) uniform sampler2D depthTexture;
layout(binding = 1) uniform sampler2D normalTexture;
layout(binding = 2) uniform sampler2D noiseTexture;

// SSAO kernel samples
layout(binding = 3) uniform SSAOKernel {
    vec4 samples[64];
} ssaoKernel;

// SSAO parameters
layout(binding = 4) uniform SSAOParams {
    mat4 projection;
    float nearPlane;
    float farPlane;
    vec2 noiseScale;
} params;


const int KERNEL_SIZE = 64;
const float RADIUS = 0.3;
const float BIAS = 0.025;

// Reconstruct view-space position from depth
vec3 reconstructViewPos(vec2 uv, float depth) {
    // Convert UV [0,1] to NDC [-1,1]
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);  // ✅ Use the parameter
    vec4 viewPos = inverse(params.projection) * clipPos;
    
    return viewPos.xyz / viewPos.w;
}


void main() {
    // Get G-Buffer data
    float depth = texture(depthTexture, fragTexCoord).r;
    
    // Early exit for skybox
    if (depth >= 1.0) {
        outSSAO = 1.0;
        return;
    }
    
    vec3 normal = normalize(texture(normalTexture, fragTexCoord).rgb);
    vec3 fragPos = reconstructViewPos(fragTexCoord, depth);

    
    // Get noise vector
    vec3 randomVec = texture(noiseTexture, fragTexCoord * params.noiseScale).xyz;
    
    // Create TBN matrix
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    // Perform SSAO sampling
    float occlusion = 0.0;
    for(int i = 0; i < KERNEL_SIZE; i++) {
        // Get sample position
        vec3 samplePos = TBN * ssaoKernel.samples[i].xyz;
        samplePos = fragPos + samplePos * RADIUS;
        
        // Project sample position
        vec4 offset = vec4(samplePos, 1.0);
        offset = params.projection * offset;
        offset.xy /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;
        
        // Get sample depth
        float sampleDepth = texture(depthTexture, offset.xy).r;
        vec3 sampleViewPos = depthToViewPos(offset.xy, sampleDepth);
        
        // Range check & accumulate
        float rangeCheck = smoothstep(0.0, 1.0, RADIUS / abs(fragPos.z - sampleViewPos.z));
        occlusion += (sampleViewPos.z >= samplePos.z + BIAS ? 1.0 : 0.0) * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    outSSAO = occlusion;
}