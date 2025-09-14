#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in vec3 fragViewPos;

// Output
layout(location = 0) out vec4 outColor;

// Descriptor Set 0: Global data
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float time;
} global;

layout(set = 0, binding = 1) uniform LightUBO {
    vec4 position[4];  // w = 0 for directional, 1 for point
    vec4 color[4];     // w = intensity (not used, intensity pre-multiplied in color)
    vec4 params;       // x = light count
} lights;

// Descriptor Set 2: Material data
layout(set = 2, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    ivec4 textureFlags; // x=hasBaseColor, y=hasMetallicRoughness, z=hasNormal, w=hasOcclusion
} material;

// Material textures
layout(set = 2, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 2, binding = 3) uniform sampler2D normalTexture;
layout(set = 2, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 2, binding = 5) uniform sampler2D emissiveTexture;

// Constants
const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;

// PBR Functions
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalTexture, fragTexCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    tangentNormal = normalize(tangentNormal);
    
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Normal Distribution Function - GGX/Trowbridge-Reitz
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

// Geometry Function - Schlick-GGX
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

// Smith's Geometry Function
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Calculate PBR lighting contribution
vec3 calculateLight(vec3 lightDir, vec3 lightColor, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);
    
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
    
    return (kD * albedo / PI + specular) * lightColor * NdotL;
}

void main() {
    // Get base color
    vec4 baseColor = material.baseColorFactor * fragColor;
    if (material.textureFlags.x == 1) {
        baseColor *= texture(baseColorTexture, fragTexCoord);
    }
    
    // Alpha test
    if (baseColor.a < material.alphaCutoff) {
        discard;
    }
    vec3 albedo = pow(baseColor.rgb, vec3(2.2));
    
    // Get metallic and roughness
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (material.textureFlags.y == 1) {
        vec4 metallicRoughness = texture(metallicRoughnessTexture, fragTexCoord);
        metallic *= metallicRoughness.b;  // Blue channel
        roughness *= metallicRoughness.g;  // Green channel
    }
    roughness = max(roughness, MIN_ROUGHNESS);
    
    // Get normal
    vec3 N = normalize(fragNormal);
    if (material.textureFlags.z == 1) {
        N = getNormalFromMap();
    }
    
    // Get ambient occlusion
    float ao = 1.0;
    if (material.textureFlags.w == 1) {
        ao = texture(occlusionTexture, fragTexCoord).r;
        ao = 1.0 + material.occlusionStrength * (ao - 1.0);
    }
    
    // Get emissive
    vec3 emissive = material.emissiveFactor;
    if (material.textureFlags.x == 1) { // Reusing flag, you might want a separate flag
        emissive *= texture(emissiveTexture, fragTexCoord).rgb;
    }
    
    // View direction
    vec3 V = normalize(fragViewPos - fragWorldPos);
    
    // Calculate reflectance at normal incidence (F0)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Calculate lighting
    vec3 Lo = vec3(0.0);
    int lightCount = int(lights.params.x);
    
    for (int i = 0; i < lightCount && i < 4; i++) {
        vec3 lightPos = lights.position[i].xyz;
        float lightType = lights.position[i].w;
        vec3 lightColor = lights.color[i].rgb;
        
        vec3 lightDir;
        float attenuation = 1.0;
        
        if (lightType == 0.0) {
            // Directional light
            lightDir = -normalize(lightPos);
        } else {
            // Point light
            lightDir = lightPos - fragWorldPos;
            float distance = length(lightDir);
            lightDir = normalize(lightDir);
            
            // Inverse square falloff
            attenuation = 1.0 / (distance * distance);
        }
        
        Lo += calculateLight(lightDir, lightColor * attenuation, N, V, albedo, metallic, roughness, F0);
    }
    
    // Ambient lighting (simple hemisphere lighting)
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    
    vec3 irradiance = vec3(0.03); // Simple ambient
    vec3 diffuse = irradiance * albedo;
    
    // Simple environment reflection approximation
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = vec3(0.01) * (1.0 - roughness); // Fake reflection
    vec2 envBRDF = vec2(0.9, 0.1 * (1.0 - roughness)); // Fake BRDF
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
    
    vec3 ambient = (kD * diffuse + specular) * ao;
    
    // Final color
    vec3 color = ambient + Lo + emissive;
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, baseColor.a);
}