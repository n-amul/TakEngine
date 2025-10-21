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
	vec4 lightDir;
	float exposure;
	float gamma;
	vec3 ambientLight;  // Added ambient light support
} uboParams;

// Material structure
struct ShaderMaterial {
	vec4 baseColorFactor;
	vec4 emissiveFactor;
	vec4 diffuseFactor;
	vec4 specularFactor;
	float workflow;
	int baseColorTextureSet;
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

// Texture samplers
layout (set = 1, binding = 0) uniform sampler2D colorMap;
layout (set = 1, binding = 1) uniform sampler2D physicalDescriptorMap;
layout (set = 1, binding = 2) uniform sampler2D normalMap;
layout (set = 1, binding = 3) uniform sampler2D aoMap;
layout (set = 1, binding = 4) uniform sampler2D emissiveMap;

// Material SSBO
layout(std430, set = 3, binding = 0) readonly buffer SSBO {
   ShaderMaterial materials[];
};

layout (push_constant) uniform PushConstants {
	int meshIndex;
	int materialIndex;
} pushConstants;

layout (location = 0) out vec4 outColor;

// PBR data structure
struct PBRInfo {
	float NdotL;
	float NdotV;
	float NdotH;
	float LdotH;
	float VdotH;
	float perceptualRoughness;
	float metalness;
	vec3 reflectance0;
	vec3 reflectance90;
	float alphaRoughness;
	vec3 diffuseColor;
	vec3 specularColor;
};

// Constants
const float M_PI = 3.141592653589793;
const float c_MinRoughness = 0.04;
const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
const float PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1.0;

// Uncharted 2 tonemapping
vec3 Uncharted2Tonemap(vec3 color) {
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}

vec4 tonemap(vec4 color) {
	vec3 outcol = Uncharted2Tonemap(color.rgb * uboParams.exposure);
	outcol = outcol * (1.0f / Uncharted2Tonemap(vec3(11.2f)));	
	return vec4(pow(outcol, vec3(1.0f / uboParams.gamma)), color.a);
}

// sRGB to Linear conversion
vec4 SRGBtoLINEAR(vec4 srgbIn) {
	vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
	vec3 linOut = mix(srgbIn.xyz/vec3(12.92), pow((srgbIn.xyz+vec3(0.055))/vec3(1.055), vec3(2.4)), bLess);
	return vec4(linOut, srgbIn.w);
}

// Get normal from normal map or vertex normal
vec3 getNormal(ShaderMaterial material) {
	if (material.normalTextureSet < 0) {
		return normalize(inNormal);
	}
	
	vec3 tangentNormal = texture(normalMap, material.normalTextureSet == 0 ? inUV0 : inUV1).xyz * 2.0 - 1.0;
	
	// Use provided tangent if available, otherwise calculate from derivatives
	vec3 N = normalize(inNormal);
	vec3 T;
	vec3 B;
	
	if (length(inTangent) > 0.0) {
		T = normalize(inTangent.xyz);
		B = normalize(cross(N, T) * inTangent.w);
	} else {
		vec3 q1 = dFdx(inWorldPos);
		vec3 q2 = dFdy(inWorldPos);
		vec2 st1 = dFdx(inUV0);
		vec2 st2 = dFdy(inUV0);
		T = normalize(q1 * st2.t - q2 * st1.t);
		B = normalize(cross(N, T));
	}
	
	mat3 TBN = mat3(T, B, N);
	return normalize(TBN * tangentNormal);
}

// Lambertian diffuse
vec3 diffuse(PBRInfo pbrInputs) {
	return pbrInputs.diffuseColor / M_PI;
}

// Fresnel Schlick
vec3 specularReflectionF(PBRInfo pbrInputs) {
	return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// Smith G function for GGX
float geometricOcclusionG(PBRInfo pbrInputs) {
	float NdotL = pbrInputs.NdotL;
	float NdotV = pbrInputs.NdotV;
	float r = pbrInputs.alphaRoughness;
	
	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

// GGX distribution
float microfacetDistributionD(PBRInfo pbrInputs) {
	float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
	float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
	return roughnessSq / (M_PI * f * f);
}

// Convert specular glossiness to metallic roughness
float convertMetallic(vec3 diffuse, vec3 specular, float maxSpecular) {
	float perceivedDiffuse = sqrt(0.299 * diffuse.r * diffuse.r + 0.587 * diffuse.g * diffuse.g + 0.114 * diffuse.b * diffuse.b);
	float perceivedSpecular = sqrt(0.299 * specular.r * specular.r + 0.587 * specular.g * specular.g + 0.114 * specular.b * specular.b);
	
	if (perceivedSpecular < c_MinRoughness) {
		return 0.0;
	}
	
	float a = c_MinRoughness;
	float b = perceivedDiffuse * (1.0 - maxSpecular) / (1.0 - c_MinRoughness) + perceivedSpecular - 2.0 * c_MinRoughness;
	float c = c_MinRoughness - perceivedSpecular;
	float D = max(b * b - 4.0 * a * c, 0.0);
	return clamp((-b + sqrt(D)) / (2.0 * a), 0.0, 1.0);
}

void main() {
	ShaderMaterial material = materials[pushConstants.materialIndex];
	
	float perceptualRoughness;
	float metallic;
	vec3 diffuseColor;
	vec4 baseColor;
	
	vec3 f0 = vec3(0.04);
	
	// Handle alpha masking
	if (material.alphaMask == 1.0f) {
		if (material.baseColorTextureSet > -1) {
			baseColor = SRGBtoLINEAR(texture(colorMap, material.baseColorTextureSet == 0 ? inUV0 : inUV1)) * material.baseColorFactor;
		} else {
			baseColor = material.baseColorFactor;
		}
		if (baseColor.a < material.alphaMaskCutoff) {
			discard;
		}
	}
	
	// Metallic Roughness workflow
	if (material.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS) {
		perceptualRoughness = material.roughnessFactor;
		metallic = material.metallicFactor;
		
		if (material.physicalDescriptorTextureSet > -1) {
			vec4 mrSample = texture(physicalDescriptorMap, material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1);
			perceptualRoughness = mrSample.g * perceptualRoughness;
			metallic = mrSample.b * metallic;
		}
		
		perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
		metallic = clamp(metallic, 0.0, 1.0);
		
		if (material.baseColorTextureSet > -1) {
			baseColor = SRGBtoLINEAR(texture(colorMap, material.baseColorTextureSet == 0 ? inUV0 : inUV1)) * material.baseColorFactor;
		} else {
			baseColor = material.baseColorFactor;
		}
	}
	
	// Specular Glossiness workflow
	if (material.workflow == PBR_WORKFLOW_SPECULAR_GLOSSINESS) {
		if (material.physicalDescriptorTextureSet > -1) {
			perceptualRoughness = 1.0 - texture(physicalDescriptorMap, material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1).a;
		} else {
			perceptualRoughness = 1.0; // Default to rough if no gloss map
		}
		
		const float epsilon = 1e-6;
		
		vec4 diffuse = SRGBtoLINEAR(texture(colorMap, inUV0));
		vec3 specular = SRGBtoLINEAR(texture(physicalDescriptorMap, inUV0)).rgb;
		
		float maxSpecular = max(max(specular.r, specular.g), specular.b);
		metallic = convertMetallic(diffuse.rgb, specular, maxSpecular);
		
		vec3 baseColorDiffusePart = diffuse.rgb * ((1.0 - maxSpecular) / (1 - c_MinRoughness) / max(1 - metallic, epsilon)) * material.diffuseFactor.rgb;
		vec3 baseColorSpecularPart = specular - (vec3(c_MinRoughness) * (1 - metallic) * (1 / max(metallic, epsilon))) * material.specularFactor.rgb;
		baseColor = vec4(mix(baseColorDiffusePart, baseColorSpecularPart, metallic * metallic), diffuse.a);
		
		perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
	}
	
	// Apply vertex color
	baseColor *= inColor0;
	
	// Calculate diffuse and specular colors
	diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
	diffuseColor *= 1.0 - metallic;
	
	float alphaRoughness = perceptualRoughness * perceptualRoughness;
	vec3 specularColor = mix(f0, baseColor.rgb, metallic);
	
	// Setup reflectance
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
	
	// Calculate vectors
	vec3 n = getNormal(material);
	vec3 v = normalize(ubo.camPos - inWorldPos);
	vec3 l = normalize(-uboParams.lightDir.xyz); // Assuming lightDir points toward light
	vec3 h = normalize(l + v);
	
	// Calculate dot products
	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);
	
	// Setup PBR inputs
	PBRInfo pbrInputs = PBRInfo(
		NdotL,
		NdotV,
		NdotH,
		LdotH,
		VdotH,
		perceptualRoughness,
		metallic,
		specularEnvironmentR0,
		specularEnvironmentR90,
		alphaRoughness,
		diffuseColor,
		specularColor
	);
	
	// Calculate BRDF
	vec3 F = specularReflectionF(pbrInputs);
	float G = geometricOcclusionG(pbrInputs);
	float D = microfacetDistributionD(pbrInputs);
	
	// Direct lighting calculation
	const vec3 u_LightColor = vec3(1.0);
	vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
	vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
	vec3 directLight = NdotL * u_LightColor * (diffuseContrib + specContrib);
	
	// Add simple ambient light as IBL substitute
	vec3 ambientColor = vec3(0.03); // Default ambient
	if (length(uboParams.ambientLight) > 0.0) {
		ambientColor = uboParams.ambientLight;
	}
	
	// Simple ambient approximation (without IBL)
	vec3 ambientDiffuse = ambientColor * diffuseColor;
	vec3 ambientSpecular = ambientColor * specularColor * (1.0 - perceptualRoughness);
	vec3 ambient = ambientDiffuse + ambientSpecular * 0.5;
	
	// Combine lighting
	vec3 color = directLight + ambient;
	
	// Apply ambient occlusion
	if (material.occlusionTextureSet > -1) {
		float ao = texture(aoMap, material.occlusionTextureSet == 0 ? inUV0 : inUV1).r;
		color = mix(color, color * ao, 1.0);
	}
	
	// Add emissive
	vec3 emissive = material.emissiveFactor.rgb * material.emissiveStrength;
	if (material.emissiveTextureSet > -1) {
		emissive *= SRGBtoLINEAR(texture(emissiveMap, material.emissiveTextureSet == 0 ? inUV0 : inUV1)).rgb;
	}
	color += emissive;
	//debug:: normal map
	outColor =vec4(n * 0.5 + 0.5, 1.0);
	// Apply tonemapping and gamma correction
	//outColor = tonemap(vec4(color, baseColor.a));
}