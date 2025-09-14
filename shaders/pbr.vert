#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;
layout(location = 6) out vec3 fragViewPos;

// Descriptor Set 0: Global data
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float time;
} global;

// Descriptor Set 1: Model data
layout(set = 1, binding = 0) uniform ModelUBO {
    mat4 model;
    mat4 normalMatrix;
} model;

void main() {
    // Transform position to world space
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform to clip space
    gl_Position = global.proj * global.view * worldPos;
    
    // Transform normal to world space (using normal matrix to handle non-uniform scaling)
    fragNormal = normalize(mat3(model.normalMatrix) * inNormal);
    
    // Calculate tangent and bitangent in world space
    vec3 T = normalize(mat3(model.normalMatrix) * inTangent.xyz);
    vec3 N = fragNormal;
    
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    
    // Calculate bitangent with handedness from w component
    vec3 B = cross(N, T) * inTangent.w;
    
    fragTangent = T;
    fragBitangent = B;
    
    // Pass through texture coordinates and vertex color
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    
    // Camera position for view direction calculations
    fragViewPos = global.cameraPos;
}