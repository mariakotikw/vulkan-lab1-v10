layout(std140, set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambientColorIntensity;
    vec4 directionalDirection;
    vec4 directionalColorIntensity;
    uvec4 lightCounts;
} scene;
