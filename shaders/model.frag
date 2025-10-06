#version 450
#extension GL_ARB_separate_shader_objects : enable

// Inputs from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in vec4 inColor;
layout(location = 5) in vec3 inViewDir;
layout(location = 6) in vec3 inLightDir;

// Output
layout(location = 0) out vec4 outFragColor;

// Set 0: Scene uniforms
layout(set = 0, binding = 0) uniform UBOMatrices {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
    float padding;
} matrices;

layout(set = 0, binding = 1) uniform UBOParams {
    vec4 lightDir;
    float exposure;
    float gamma;
    vec2 padding;
} params;

// Set 1: Material textures (5 texture slots)
layout(set = 1, binding = 0) uniform sampler2D colorMap;
layout(set = 1, binding = 1) uniform sampler2D physicalDescriptorMap; // metallic(B) roughness(G)
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// Set 2: Material properties SSBO
// Define the material structure to match C++ ShaderMaterial
struct Material {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float workflow;
    int colorTextureSet;
    int physicalDescriptorTextureSet;
    int normalTextureSet;
    int occlusionTextureSet;
    int emissiveTextureSet;
    float metallicFactor;
    float roughnessFactor;
    float alphaMask;
    float alphaMaskCutoff;
};

layout(std430, set = 2, binding = 0) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

// Push constants
layout(push_constant) uniform PushConstants {
    int meshIndex;
    int materialIndex;
} pushConstants;

// Constants
const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;

// Get normal from normal map
vec3 getNormalFromMap(int normalTexSet, vec2 uv0, vec2 uv1) {
    vec2 uv = normalTexSet == 0 ? uv0 : uv1;
    
    vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    
    // Reconstruct TBN matrix
    vec3 Q1 = dFdx(inWorldPos);
    vec3 Q2 = dFdy(inWorldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);
    
    vec3 N = normalize(inNormal);
    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

// Fresnel equation (Schlick approximation)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

// Geometry function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Geometry function combining view and light directions (Smith method)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// sRGB to linear conversion
vec4 sRGBToLinear(vec4 srgbIn) {
    vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
    vec3 linOut = mix(srgbIn.xyz / vec3(12.92), 
                      pow((srgbIn.xyz + vec3(0.055)) / vec3(1.055), vec3(2.4)), 
                      bLess);
    return vec4(linOut, srgbIn.w);
}

void main() {
    // Get material using push constant index
    Material material = materialBuffer.materials[pushConstants.materialIndex];
    
    // Select UV set based on material
    vec2 baseUV = material.colorTextureSet == 0 ? inUV0 : inUV1;
    
    // Get base color
    vec4 baseColor = sRGBToLinear(texture(colorMap, baseUV)) * material.baseColorFactor;
    baseColor *= inColor; // Apply vertex color
    
    // Alpha test
    if (material.alphaMask > 0.5) {
        if (baseColor.a < material.alphaMaskCutoff) {
            discard;
        }
    }
    
    // Get metallic and roughness values
    vec2 physicalUV = material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1;
    vec3 orm = texture(physicalDescriptorMap, physicalUV).rgb;
    float roughness = orm.g * material.roughnessFactor;
    float metallic = orm.b * material.metallicFactor;
    
    roughness = clamp(roughness, MIN_ROUGHNESS, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    
    // Get normal
    vec3 N = normalize(inNormal);
    if (material.normalTextureSet >= 0) {
        N = getNormalFromMap(material.normalTextureSet, inUV0, inUV1);
    }
    
    vec3 V = normalize(matrices.camPos - inWorldPos);
    vec3 L = normalize(-params.lightDir.xyz);
    vec3 H = normalize(V + L);
    vec3 reflection = -normalize(reflect(V, N));
    
    // Calculate reflectance at normal incidence (F0)
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, baseColor.rgb, metallic);
    
    // Calculate radiance
    vec3 lightColor = vec3(1.0);
    float lightIntensity = 5.0;
    vec3 radiance = lightColor * lightIntensity;
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
    
    // Ambient occlusion
    float ao = 1.0;
    if (material.occlusionTextureSet >= 0) {
        vec2 aoUV = material.occlusionTextureSet == 0 ? inUV0 : inUV1;
        ao = texture(aoMap, aoUV).r;
    }
    
    // Simple ambient term (without IBL)
    vec3 ambient = vec3(0.03) * baseColor.rgb * ao;
    
    // Emissive
    vec3 emissive = material.emissiveFactor.rgb;
    if (material.emissiveTextureSet >= 0) {
        vec2 emissiveUV = material.emissiveTextureSet == 0 ? inUV0 : inUV1;
        emissive *= sRGBToLinear(texture(emissiveMap, emissiveUV)).rgb;
    }
    
    // Combine all lighting
    vec3 color = ambient + Lo + emissive;
    
    // Exposure tone mapping
    color = vec3(1.0) - exp(-color * params.exposure);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / params.gamma));
    
    outFragColor = vec4(color, baseColor.a);
}