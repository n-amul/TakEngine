#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Constants
const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;

// Inputs from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in vec4 inColor;
layout(location = 5) in vec4 inTangent;

// Output
layout(location = 0) out vec4 outColor;

// Uniform buffers
layout(set = 0, binding = 0) uniform UBOMatrices {
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
} ubo;

layout(set = 0, binding = 1) uniform UBOParams {
    vec4 lightDir;
    float exposure;
    float gamma;
} params;

// Material textures
layout(set = 1, binding = 0) uniform sampler2D colorMap;
layout(set = 1, binding = 1) uniform sampler2D physicalDescriptorMap; // metallic/roughness or specular/glossiness
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// Material data SSBO
struct Material {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 diffuseFactor;
    vec4 specularFactor;
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
    float emissiveStrength;
    float padding;
};

layout(set = 3, binding = 0) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

// Push constants
layout(push_constant) uniform PushConsts {
    int meshIndex;
    int materialIndex;
} pushConsts;

// PBR Functions
vec3 getNormal() {
    vec3 tangentNormal = inNormal;
    int matIdx = pushConsts.materialIndex;
    
    if (materialBuffer.materials[matIdx].normalTextureSet > -1) {
        // Get UV coordinates based on texture set
        vec2 uv = (materialBuffer.materials[matIdx].normalTextureSet == 0) ? inUV0 : inUV1;
        tangentNormal = texture(normalMap, uv).rgb * 2.0 - 1.0;
        
        // Construct TBN matrix
        vec3 N = normalize(inNormal);
        vec3 T = normalize(inTangent.xyz);
        vec3 B = normalize(cross(N, T) * inTangent.w);
        mat3 TBN = mat3(T, B, N);
        
        return normalize(TBN * tangentNormal);
    }
    
    return normalize(tangentNormal);
}

// Fresnel Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution
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

// Schlick-GGX geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Smith's geometry function
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 calculatePBR(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L) {
    vec3 H = normalize(V + L);
    roughness = max(roughness, MIN_ROUGHNESS);
    
    // Calculate reflectance at normal incidence
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Calculate BRDF components
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
    
    return (kD * albedo / PI + specular) * NdotL;
}

void main() {
    // Get material properties using index
    int matIdx = pushConsts.materialIndex;
    Material mat = materialBuffer.materials[matIdx];
    
    vec4 baseColor = mat.baseColorFactor;
    
    // Sample base color texture if available
    if (mat.colorTextureSet > -1) {
        vec2 uv = (mat.colorTextureSet == 0) ? inUV0 : inUV1;
        baseColor *= texture(colorMap, uv);
    }
    
    // Apply vertex color
    baseColor *= inColor;
    
    // Alpha masking
    if (mat.alphaMask == 1.0) {
        if (baseColor.a < mat.alphaMaskCutoff) {
            discard;
        }
    }
    
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    
    // Metallic-Roughness workflow
    if (mat.workflow == 0.0) {
        if (mat.physicalDescriptorTextureSet > -1) {
            vec2 uv = (mat.physicalDescriptorTextureSet == 0) ? inUV0 : inUV1;
            vec4 mrSample = texture(physicalDescriptorMap, uv);
            roughness *= mrSample.g;
            metallic *= mrSample.b;
        }
    }
    // Specular-Glossiness workflow
    else if (mat.workflow == 1.0) {
        baseColor = mat.diffuseFactor;
        if (mat.colorTextureSet > -1) {
            vec2 uv = (mat.colorTextureSet == 0) ? inUV0 : inUV1;
            baseColor *= texture(colorMap, uv);
        }
        
        if (mat.physicalDescriptorTextureSet > -1) {
            vec2 uv = (mat.physicalDescriptorTextureSet == 0) ? inUV0 : inUV1;
            vec4 sgSample = texture(physicalDescriptorMap, uv);
            roughness = 1.0 - sgSample.a; // Glossiness to roughness
            vec3 specular = mat.specularFactor.rgb * sgSample.rgb;
            float maxSpecular = max(max(specular.r, specular.g), specular.b);
            metallic = maxSpecular;
        }
    }
    
    // Get normal
    vec3 N = getNormal();
    vec3 V = normalize(ubo.camPos - inWorldPos);
    
    // Simple directional light
    vec3 L = normalize(-params.lightDir.xyz);
    vec3 color = calculatePBR(baseColor.rgb, metallic, roughness, N, V, L);
    
    // Ambient occlusion
    float ao = 1.0;
    if (mat.occlusionTextureSet > -1) {
        vec2 uv = (mat.occlusionTextureSet == 0) ? inUV0 : inUV1;
        ao = texture(aoMap, uv).r;
    }
    
    // Simple ambient lighting
    vec3 ambient = vec3(0.03) * baseColor.rgb * ao;
    color += ambient;
    
    // Emissive
    vec3 emissive = mat.emissiveFactor.rgb * mat.emissiveStrength;
    if (mat.emissiveTextureSet > -1) {
        vec2 uv = (mat.emissiveTextureSet == 0) ? inUV0 : inUV1;
        emissive *= texture(emissiveMap, uv).rgb;
    }
    color += emissive;
    
    // Tone mapping and gamma correction
    color = vec3(1.0) - exp(-color * params.exposure);
    color = pow(color, vec3(1.0 / params.gamma));
    
    outColor = vec4(color, baseColor.a);
}