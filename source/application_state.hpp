#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace application {

enum class Projection { Perspective, Orthographic };

struct Transform {
	glm::vec3 position{ 0.0f };
	glm::vec3 rotation_degrees{ 5.0f, 10.0f, 0.0f };
	glm::vec3 scale{ 1.0f };
};

struct Animation {
	bool playing = false;
	float speed = 1.0f;
	float radius = 0.75f;
	float vertical_amplitude = 0.4f;
	double phase = 0.0;
	double last_time = 0.0;
	bool clock_initialized = false;

	void advance(double time);
	void reset();
	glm::vec3 offset() const;
	glm::vec3 rotation() const;
};

struct ProjectionState {
	Projection projection = Projection::Perspective;
	float fov_degrees = 45.0f;
	float ortho_half_height = 2.5f;

	void sanitize();
	glm::mat4 projectionMatrix(float aspect) const;
};

struct ObjectState {
	Transform transform;
	Transform initial_transform;
	Animation animation;
	glm::vec3 user_color{ 1.0f };

	void resetTransform();
	void sanitize();
	glm::mat4 modelMatrix() const;
};

struct alignas(16) TransformUniform {
	glm::mat4 mvp;
	glm::vec4 user_color;
};
static_assert(std::is_standard_layout_v<TransformUniform>);
static_assert(offsetof(TransformUniform, mvp) == 0);
static_assert(offsetof(TransformUniform, user_color) == 64);
static_assert(sizeof(TransformUniform) == 80);
static_assert(alignof(TransformUniform) == 16);

inline constexpr uint32_t object_count = 2;
struct SceneState {
	ProjectionState camera;
	std::array<ObjectState, object_count> objects;
	int selected_object = 0;

	SceneState();
	void advance(double time);
	void sanitize();
	std::array<TransformUniform, object_count> uniforms(float aspect) const;
};

}
