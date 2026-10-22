#include "lighting.hpp"

#include <algorithm>
#include <cmath>

namespace application {
namespace {
float finiteClamp(float value, float low, float high, float fallback) {
	return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
glm::vec3 colorClamp(glm::vec3 color) {
	for (int i = 0; i < 3; ++i) color[i] = finiteClamp(color[i], 0, 1, 1);
	return color;
}
}

glm::vec3 safeDirection(glm::vec3 direction, glm::vec3 fallback) {
	const float length_squared = glm::dot(direction, direction);
	return std::isfinite(length_squared) && length_squared > 1e-8f
		? direction / std::sqrt(length_squared) : fallback;
}
void Material::sanitize() {
	albedo = colorClamp(albedo);
	specular = colorClamp(specular);
	shininess = finiteClamp(shininess, 1, 256, 48);
}
void DirectionalLight::sanitize() {
	direction = safeDirection(direction);
	color = colorClamp(color);
	intensity = finiteClamp(intensity, 0, 10, 0);
}
void PointLight::sanitize() {
	for (int i = 0; i < 3; ++i) position[i] = finiteClamp(position[i], -1000, 1000, 0);
	color = colorClamp(color);
	intensity = finiteClamp(intensity, 0, 1000, 0);
}
GpuPointLight PointLight::gpu() const {
	return { glm::vec4(position, 1), glm::vec4(color, intensity) };
}
void SpotLight::sanitize() {
	PointLight::sanitize();
	direction = safeDirection(direction);
	inner_degrees = finiteClamp(inner_degrees, 0, 89, 18);
	outer_degrees = finiteClamp(outer_degrees, 0, 89, 28);
	if (inner_degrees > outer_degrees) std::swap(inner_degrees, outer_degrees);
}
GpuSpotLight SpotLight::gpu() const {
	SpotLight valid = *this;
	valid.sanitize();
	return { glm::vec4(valid.position, 1),
	         glm::vec4(valid.direction, std::cos(glm::radians(valid.inner_degrees))),
	         glm::vec4(valid.color, valid.intensity),
	         glm::vec4(std::cos(glm::radians(valid.outer_degrees)), 0, 0, 0) };
}
float inverseSquareAttenuation(float distance_squared) {
	return 1.0f / std::max(distance_squared, 0.01f);
}
float spotConeFactor(float cosine, float inner_cosine, float outer_cosine) {
	if (inner_cosine - outer_cosine <= 1e-6f) return cosine >= outer_cosine ? 1.0f : 0.0f;
	const float t = std::clamp((cosine - outer_cosine) / (inner_cosine - outer_cosine), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
}
