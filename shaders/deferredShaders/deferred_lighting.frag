#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// G-Buffer inputs
layout(binding = 0) uniform sampler2D gNormalMetallic;   // RGB=normal (view-space), A=metallic
layout(binding = 1) uniform sampler2D gAlbedoAO;         // RGB=albedo, A=AO
layout(binding = 2) uniform sampler2D gMaterial;          // R=roughness, GBA=emissive
layout(binding = 3) uniform sampler2D gDepth;             // Depth buffer
layout(binding = 4) uniform sampler2D gSSAO;              // Blurred SSAO

#define MAX_POINT_LIGHTS 32

struct PointLight {
    vec4 position;   // xyz=pos, w=radius
    vec4 color;      // xyz=color, w=intensity
};

struct DirectionalLight {
    vec4 direction;  // xyz=dir, w=unused
    vec4 color;      // xyz=color, w=intensity
};

layout(binding = 5) uniform LightUBO {
    mat4 invView;
    mat4 invProj;
    vec4 cameraPos;
    DirectionalLight sunLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
    int numPointLights;
    float ambientIntensity;
    float ssaoStrength;
    float padding;
} lights;

const float PI = 3.14159265359;

// Reconstruct world position from depth
vec3 reconstructWorldPos(vec2 uv, float depth) {
    // UV to NDC: [0,1] -> [-1,1]
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = lights.invProj * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = lights.invView * viewPos;
    return worldPos.xyz;
}

// PBR functions
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Compute radiance from a single light
vec3 computeLight(vec3 L, vec3 radiance, vec3 N, vec3 V,
                  vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main() {
    // Sample G-Buffer
    vec4 normalMetallic = texture(gNormalMetallic, fragUV);
    vec4 albedoAO = texture(gAlbedoAO, fragUV);
    vec4 materialData = texture(gMaterial, fragUV);
    float depth = texture(gDepth, fragUV).r;
    float ssao = texture(gSSAO, fragUV).r;

    // Early out for sky pixels (depth == 1.0 means nothing was drawn)
    if (depth >= 1.0) {
        outColor = vec4(0.02, 0.02, 0.03, 1.0);  // background color
        return;
    }

    // Unpack G-Buffer
    // FIX: Normals are stored in R16G16B16A16_SFLOAT which natively holds [-1,1].
    //      No decode needed — just normalize to handle interpolation artifacts.
    vec3 viewN = normalize(normalMetallic.rgb);
    float metallic = normalMetallic.a;
    vec3 albedo = albedoAO.rgb;
    float ao = albedoAO.a;
    float roughness = materialData.r;
    vec3 emissive = materialData.gba;

    // FIX: Transform normal from view space to world space so it matches
    //      world-space light directions and camera position.
    vec3 N = normalize(mat3(lights.invView) * viewN);

    // Reconstruct world position
    vec3 worldPos = reconstructWorldPos(fragUV, depth);
    vec3 V = normalize(lights.cameraPos.xyz - worldPos);

    // Base reflectance (dielectric = 0.04, metallic = albedo)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Combine SSAO with geometric AO
    float combinedAO = ao * mix(1.0, ssao, lights.ssaoStrength);

    vec3 Lo = vec3(0.0);

    // Directional light (sun)
    {
        vec3 L = normalize(-lights.sunLight.direction.xyz);
        vec3 radiance = lights.sunLight.color.rgb * lights.sunLight.color.w;
        Lo += computeLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // Point lights
    for (int i = 0; i < lights.numPointLights; i++) {
        vec3 lightPos = lights.pointLights[i].position.xyz;
        float radius = lights.pointLights[i].position.w;
        vec3 lightColor = lights.pointLights[i].color.rgb;
        float intensity = lights.pointLights[i].color.w;

        vec3 L = lightPos - worldPos;
        float dist = length(L);
        L = normalize(L);

        // Attenuation with radius falloff
        float attenuation = 1.0 / (dist * dist);
        float falloff = clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0);
        falloff = falloff * falloff;

        vec3 radiance = lightColor * intensity * attenuation * falloff;
        Lo += computeLight(L, radiance, N, V, albedo, metallic, roughness, F0);
    }

    // Ambient
    vec3 ambient = lights.ambientIntensity * albedo * combinedAO;

    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
