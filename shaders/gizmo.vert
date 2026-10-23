#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 0) out vec3 markerColor;
void main() {
    gl_Position = scene.viewProjection * vec4(position, 1.0);
    markerColor = color;
}
