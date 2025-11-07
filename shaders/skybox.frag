#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 inUVW;
layout (location = 0) out vec4 outColor;
//later for SRGB to LinearConversion
layout (set = 0, binding = 1) uniform UBOParams {
	vec4 _pad0;
	float exposure;
	float gamma;
} uboParams;

layout (binding = 2) uniform samplerCube samplerEnv;


void main() 
{
	outColor = texture(samplerEnv, inUVW);
}