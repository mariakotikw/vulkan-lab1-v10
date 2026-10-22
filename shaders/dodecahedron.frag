#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "object.glsl"
struct PointLight { vec4 position; vec4 colorIntensity; };
struct SpotLight { vec4 position; vec4 directionInnerCos; vec4 colorIntensity; vec4 outerCos; };
layout(std430, set = 0, binding = 1) readonly buffer Points { PointLight lights[]; } points;
layout(std430, set = 0, binding = 2) readonly buffer Spots { SpotLight lights[]; } spots;
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;
vec3 safeNormalize(vec3 v) { return v * inversesqrt(max(dot(v, v), 1e-12)); }
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance) {
    float diffuse = max(dot(N, L), 0.0);
    if (diffuse <= 0.0) return vec3(0.0);
    vec3 H = safeNormalize(L + V);
    float specular = pow(max(dot(N, H), 0.0), object.specularShininess.w);
    return radiance * (object.albedo.rgb * diffuse + object.specularShininess.rgb * specular);
}
float inverseSquare(vec3 delta) { return 1.0 / max(dot(delta, delta), 0.01); }
float coneFactor(float cosine, float innerCos, float outerCos) {
    if (innerCos - outerCos <= 1e-6) return step(outerCos, cosine);
    return smoothstep(outerCos, innerCos, cosine);
}
void main() {
    vec3 N = safeNormalize(worldNormal);
    vec3 V = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 color = scene.ambientColorIntensity.rgb * scene.ambientColorIntensity.w * object.albedo.rgb;
    color += blinnPhong(N, V, safeNormalize(-scene.directionalDirection.xyz),
                      scene.directionalColorIntensity.rgb * scene.directionalColorIntensity.w);
    for (uint i = 0; i < scene.lightCounts.x; ++i) {
        PointLight light = points.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        color += blinnPhong(N, V, safeNormalize(delta), light.colorIntensity.rgb * light.colorIntensity.w * inverseSquare(delta));
    }
    for (uint i = 0; i < scene.lightCounts.y; ++i) {
        SpotLight light = spots.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        vec3 L = safeNormalize(delta);
        float cone = coneFactor(dot(-L, light.directionInnerCos.xyz), light.directionInnerCos.w, light.outerCos.x);
        color += blinnPhong(N, V, L, light.colorIntensity.rgb * light.colorIntensity.w * inverseSquare(delta) * cone);
    }
    outColor = vec4(color, 1.0);
}
