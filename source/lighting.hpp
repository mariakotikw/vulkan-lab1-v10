#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <type_traits>

namespace application {

struct Material {
	glm::vec3 albedo{ 1.0f };
	glm::vec3 specular{ 0.7f };
	float shininess = 48.0f;
	void sanitize();
};

struct DirectionalLight {
	glm::vec3 direction{ -0.5f, -1.0f, -0.8f }; // Direction in which rays travel.
	glm::vec3 color{ 1.0f, 0.95f, 0.85f };
	float intensity = 0.8f;
	void sanitize();
};

// vec4-only GPU records: no compiler-dependent vec3/bool padding.
struct alignas(16) GpuPointLight {
	glm::vec4 position;
	glm::vec4 color_intensity;
};
struct alignas(16) GpuSpotLight {
	glm::vec4 position;
	glm::vec4 direction_inner_cos;
	glm::vec4 color_intensity;
	glm::vec4 outer_cos;
};

struct PointLight {
	glm::vec3 position{ 0.0f, 2.0f, 2.0f };
	glm::vec3 color{ 1.0f };
	float intensity = 6.0f;
	void sanitize();
	GpuPointLight gpu() const;
};
struct SpotLight : PointLight {
	glm::vec3 direction{ 0.0f, -0.7f, -1.0f };
	float inner_degrees = 18.0f;
	float outer_degrees = 28.0f;
	void sanitize();
	GpuSpotLight gpu() const;
};

struct alignas(16) SceneUniform {
	glm::mat4 view_projection;
	glm::vec4 camera_position;
	glm::vec4 ambient_color_intensity;
	glm::vec4 directional_direction;
	glm::vec4 directional_color_intensity;
	glm::uvec4 light_counts; // x = point count, y = spot count
};
struct alignas(16) ObjectUniform {
	glm::mat4 model;
	glm::mat4 normal_matrix;
	glm::vec4 albedo;
	glm::vec4 specular_shininess;
};

static_assert(std::is_standard_layout_v<SceneUniform> && std::is_standard_layout_v<ObjectUniform>);
static_assert(std::is_standard_layout_v<GpuPointLight> && std::is_standard_layout_v<GpuSpotLight>);
static_assert(sizeof(SceneUniform) == 144 && alignof(SceneUniform) == 16);
static_assert(offsetof(SceneUniform, camera_position) == 64);
static_assert(offsetof(SceneUniform, ambient_color_intensity) == 80);
static_assert(offsetof(SceneUniform, directional_direction) == 96);
static_assert(offsetof(SceneUniform, directional_color_intensity) == 112);
static_assert(offsetof(SceneUniform, light_counts) == 128);
static_assert(sizeof(ObjectUniform) == 160 && alignof(ObjectUniform) == 16);
static_assert(offsetof(ObjectUniform, model) == 0 && offsetof(ObjectUniform, normal_matrix) == 64);
static_assert(offsetof(ObjectUniform, albedo) == 128 && offsetof(ObjectUniform, specular_shininess) == 144);
static_assert(sizeof(GpuPointLight) == 32 && alignof(GpuPointLight) == 16);
static_assert(offsetof(GpuPointLight, position) == 0 && offsetof(GpuPointLight, color_intensity) == 16);
static_assert(sizeof(GpuSpotLight) == 64 && alignof(GpuSpotLight) == 16);
static_assert(offsetof(GpuSpotLight, position) == 0 && offsetof(GpuSpotLight, direction_inner_cos) == 16);
static_assert(offsetof(GpuSpotLight, color_intensity) == 32 && offsetof(GpuSpotLight, outer_cos) == 48);

glm::vec3 safeDirection(glm::vec3 direction, glm::vec3 fallback = { 0, -1, 0 });
float inverseSquareAttenuation(float distance_squared);
float spotConeFactor(float cosine, float inner_cosine, float outer_cosine);

}
