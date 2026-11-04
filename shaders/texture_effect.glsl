layout(push_constant) uniform TexturePush {
    vec4 animation; // time, speed, UV distortion, sample blend
    vec4 controls;  // effect enabled, color modulation, albedo enabled, specular enabled
    vec4 emission;  // emissive enabled, strength, reserved, reserved
} texturePush;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

vec3 sampleAlbedo(vec2 uv) {
    if (texturePush.controls.z < 0.5) return vec3(1.0);
    vec3 original = texture(albedoMap, uv).rgb;
    if (texturePush.controls.x < 0.5) return original;
    float phase = texturePush.animation.x * texturePush.animation.y;
    float angle = 0.16 * sin(phase * 0.6);
    float c = cos(angle), s = sin(angle);
    vec2 alternate = mat2(c, s, -s, c) * (uv - 0.5) + 0.5;
    alternate += vec2(sin(phase * 0.7), cos(phase * 0.5)) * 0.018;
    alternate += texturePush.animation.z * vec2(
        sin(uv.y * 25.0 + phase), cos(uv.x * 21.0 - phase * 0.8));
    vec3 interference = texture(albedoMap, alternate).rgb;
    vec3 result = mix(original, interference, texturePush.animation.w);
    vec3 rainbow = 0.72 + 0.28 * cos(phase + uv.x * 3.0 + vec3(0.0, 2.094395, 4.18879));
    return result * mix(vec3(1.0), rainbow, texturePush.controls.y);
}
