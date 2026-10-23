#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "object.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
void main() {
    vec4 world = object.model * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * normal;
    gl_Position = scene.viewProjection * world;
}
