#version 450

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV0;
layout(location = 3) in vec2 fragUV1;
layout(location = 4) in vec4 fragColor;
layout(location = 5) in vec3 fragViewPos;
layout(location = 6) in vec3 fragLightPos;
layout(location = 7) in flat uint fragMaterialIndex;

// Output
layout(location = 0) out vec4 outColor;

// Texture samplers
layout(binding = 1) uniform sampler2D baseColorTexture;
layout(binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(binding = 3) uniform sampler2D normalTexture;
layout(binding = 4) uniform sampler2D occlusionTexture;
layout(binding = 5) uniform sampler2D emissiveTexture;

// Constants
const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;

// PBR functions
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalTexture, fragUV0).xyz * 2.0 - 1.0;
    
    vec3 Q1 = dFdx(fragWorldPos);
    vec3 Q2 = dFdy(fragWorldPos);
    vec2 st1 = dFdx(fragUV0);
    vec2 st2 = dFdy(fragUV0);
    
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

// Geometry Function (Smith's method with GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel Equation (Schlick's approximation)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Sample textures
    vec4 baseColor = texture(baseColorTexture, fragUV0) * fragColor;
    vec3 metallicRoughness = texture(metallicRoughnessTexture, fragUV0).rgb;
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;
    float ao = texture(occlusionTexture, fragUV0).r;
    vec3 emissive = texture(emissiveTexture, fragUV0).rgb;
    
    // Get normal from normal map
    vec3 N = getNormalFromMap();
    vec3 V = normalize(fragViewPos - fragWorldPos);
    
    // Calculate F0 (base reflectivity)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, baseColor.rgb, metallic);
    
    // Direct lighting (single light source for simplicity)
    vec3 L = normalize(fragLightPos - fragWorldPos);
    vec3 H = normalize(V + L);
    float distance = length(fragLightPos - fragWorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = vec3(10.0) * attenuation; // Light color and intensity
    
    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
    
    // Ambient lighting (simple IBL approximation)
    vec3 ambient = vec3(0.03) * baseColor.rgb * ao;
    
    // Combine lighting
    vec3 color = ambient + Lo + emissive;
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, baseColor.a);
}