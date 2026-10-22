layout(std140, set = 1, binding = 0) uniform Object {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specularShininess;
} object;
