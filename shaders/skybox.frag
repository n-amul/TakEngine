#version 450

layout(binding = 1) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 texCoords;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(skyboxSampler, texCoords);
    
    // Optional: Add tone mapping or color adjustment
    // outColor.rgb = pow(outColor.rgb, vec3(2.2)); // Gamma correction
    // outColor.rgb = outColor.rgb / (outColor.rgb + vec3(1.0)); // Simple tone mapping
}