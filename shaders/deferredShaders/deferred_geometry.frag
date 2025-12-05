#version 450

// Inputs from vertex shader (VIEW SPACE)
layout(location = 0) in vec3 fragNormalView;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPosView;

// Texture sampler
layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

// G-Buffer outputs (MRT)
layout(location = 0) out vec4 outNormal;    // RGB=normal, A=metallic
layout(location = 1) out vec4 outAlbedo;    // RGB=albedo, A=AO
layout(location = 2) out vec4 outMaterial;  // R=roughness, GBA=emissive

void main() {
    // Sample albedo texture
    vec3 albedo = texture(albedoSampler, fragTexCoord).rgb;
    
    // Normalize view-space normal (NO encoding - SFLOAT stores [-1,1] directly)
    vec3 N = normalize(fragNormalView);
    
    // Material properties (hardcoded for demo, you can make these uniforms)
    float metallic = 0.0;
    float roughness = 0.5;
    float ao = 1.0;
    vec3 emissive = vec3(0.0);
    
    // Write to G-Buffer
    outNormal = vec4(N, metallic);
    outAlbedo = vec4(albedo, ao);
    outMaterial = vec4(roughness, emissive);
}