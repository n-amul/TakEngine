#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outBlurred;

layout(set = 0, binding = 0) uniform sampler2D ssaoInput;
layout(set = 0, binding = 1) uniform sampler2D depthMap;  // Optional: for edge-aware blur

// Push constant or UBO for blur parameters
layout(push_constant) uniform BlurParams {
    vec2 direction;  // (1,0) for horizontal, (0,1) for vertical
    float sharpness; // Edge-aware sharpness
} params;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    
    float centerValue = texture(ssaoInput, inUV).r;
    float centerDepth = texture(depthMap, inUV).r;
    
    float result = centerValue;
    float totalWeight = 1.0;
    
    // Gaussian weights for 7-tap filter
    const float weights[4] = float[](0.324, 0.232, 0.0855, 0.0205);
    
    for (int i = 1; i <= 3; ++i) {
        vec2 offset = params.direction * texelSize * float(i);
        
        // Positive direction
        {
            float sampleValue = texture(ssaoInput, inUV + offset).r;
            float sampleDepth = texture(depthMap, inUV + offset).r;
            
            // Edge-aware weight based on depth difference
            float depthDiff = abs(centerDepth - sampleDepth);
            float edgeWeight = exp(-depthDiff * params.sharpness);
            
            float weight = weights[i] * edgeWeight;
            result += sampleValue * weight;
            totalWeight += weight;
        }
        
        // Negative direction
        {
            float sampleValue = texture(ssaoInput, inUV - offset).r;
            float sampleDepth = texture(depthMap, inUV - offset).r;
            
            float depthDiff = abs(centerDepth - sampleDepth);
            float edgeWeight = exp(-depthDiff * params.sharpness);
            
            float weight = weights[i] * edgeWeight;
            result += sampleValue * weight;
            totalWeight += weight;
        }
    }
    
    outBlurred = result / totalWeight;
}