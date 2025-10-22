#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;
layout (location = 4) in vec4 inColor0;
layout (location = 5) in vec4 inTangent;

// Scene bindings
layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

layout (set = 0, binding = 1) uniform UBOParams {
	vec3 lightPos;
	float exposure;
	float gamma;
	vec3 ambientLight;
} uboParams;

// Material structure
struct ShaderMaterial {
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
};

// Material SSBO
layout(std430, set = 3, binding = 0) readonly buffer SSBO {
   ShaderMaterial materials[];
};

// Texture samplers
layout (set = 1, binding = 0) uniform sampler2D colorMap;
layout (set = 1, binding = 1) uniform sampler2D physicalDescriptorMap;
layout (set = 1, binding = 2) uniform sampler2D normalMap;
layout (set = 1, binding = 3) uniform sampler2D aoMap;
layout (set = 1, binding = 4) uniform sampler2D emissiveMap;

layout (push_constant) uniform PushConstants {
	int meshIndex;
	int materialIndex;
} pushConstants;

layout (location = 0) out vec4 outColor;

// Constants
const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;
const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;

// PBR Functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = M_PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 getNormal() {
    vec3 tangentNormal = inNormal;
    
    ShaderMaterial material = materials[pushConstants.materialIndex];
    
    if (material.normalTextureSet > -1) {
        tangentNormal = texture(normalMap, material.normalTextureSet == 0 ? inUV0 : inUV1).rgb * 2.0 - 1.0;
        
        vec3 q1 = dFdx(inWorldPos);
        vec3 q2 = dFdy(inWorldPos);
        vec2 st1 = dFdx(inUV0);
        vec2 st2 = dFdy(inUV0);
        
        vec3 N = normalize(inNormal);
        vec3 T = normalize(q1 * st2.t - q2 * st1.t);
        vec3 B = -normalize(cross(N, T));
        mat3 TBN = mat3(T, B, N);
        
        return normalize(TBN * tangentNormal);
    }
    
    return normalize(inNormal);
}

void main() {
    ShaderMaterial material = materials[pushConstants.materialIndex];
    
    // Get base color
    vec3 albedo = material.baseColorFactor.rgb;
    if (material.colorTextureSet > -1) {
        vec4 albedoMap = texture(colorMap, material.colorTextureSet == 0 ? inUV0 : inUV1);
        albedo *= albedoMap.rgb;
    }
    albedo *= inColor0.rgb;
    
    // Get metallic and roughness
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    
    if (material.physicalDescriptorTextureSet > -1) {
        vec4 physicalDescriptor = texture(physicalDescriptorMap, 
            material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1);
        // Assuming metallic-roughness workflow: G = roughness, B = metallic
        roughness *= physicalDescriptor.g;
        metallic *= physicalDescriptor.b;
    }
    
    roughness = clamp(roughness, c_MinRoughness, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    
    // Get ambient occlusion
    float ao = 1.0;
    if (material.occlusionTextureSet > -1) {
        ao = texture(aoMap, material.occlusionTextureSet == 0 ? inUV0 : inUV1).r;
    }
    
    // Setup vectors
    vec3 N = getNormal();
    vec3 V = normalize(ubo.camPos - inWorldPos);
    
    // Calculate F0 (base reflectivity)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Main lighting calculation
    vec3 Lo = vec3(0.0);
    
    // For simplicity, using single light from uboParams
    // You can extend this to multiple lights if needed
    vec3 L = normalize(uboParams.lightPos - inWorldPos);
    vec3 H = normalize(V + L);
    float distance = length(uboParams.lightPos - inWorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = vec3(1.0) * attenuation * 20.0; // Adjust intensity as needed
    
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
    Lo += (kD * albedo / M_PI + specular) * radiance * NdotL;
    
    // Ambient lighting
    vec3 ambient = uboParams.ambientLight * albedo * ao;
    
    // Add emissive
    vec3 emissive = material.emissiveFactor.rgb * material.emissiveStrength;
    if (material.emissiveTextureSet > -1) {
        emissive *= texture(emissiveMap, material.emissiveTextureSet == 0 ? inUV0 : inUV1).rgb;
    }
    
    vec3 color = ambient + Lo + emissive;
    
    // Exposure tone mapping
    color = vec3(1.0) - exp(-color * uboParams.exposure);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / uboParams.gamma));
    
    // Alpha handling
    float alpha = material.baseColorFactor.a;
    if (material.colorTextureSet > -1) {
        alpha *= texture(colorMap, material.colorTextureSet == 0 ? inUV0 : inUV1).a;
    }
    
    // Alpha mask
    if (material.alphaMask == 1.0) {
        if (alpha < material.alphaMaskCutoff) {
            discard;
        }
    }
    
    outColor = vec4(color, alpha);
}