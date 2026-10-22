#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>
#include "lighting.hpp"

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
	float ortho_half_height = 3.2f;
	float near_plane = 0.1f;
	float far_plane = 100.0f;
	glm::vec3 position{ 0.0f, 2.5f, 9.0f };
	float yaw_degrees = -90.0f;
	float pitch_degrees = -15.52411f;

	void sanitize();
	void reset();
	glm::vec3 forward() const;
	glm::mat4 viewMatrix() const;
	glm::mat4 projectionMatrix(float aspect) const;
};

struct ObjectState {
	Transform transform;
	Transform initial_transform;
	Animation animation;
	Material material;

	void resetTransform();
	void sanitize();
	glm::mat4 modelMatrix() const;
};

inline constexpr uint32_t object_count = 2;
struct SceneState {
	ProjectionState camera;
	std::array<ObjectState, object_count> objects;
	int selected_object = 0;
	glm::vec3 ambient_color{ 1.0f };
	float ambient_intensity = 0.08f;
	DirectionalLight directional;
	std::vector<PointLight> point_lights;
	std::vector<SpotLight> spot_lights;
	bool show_light_gizmos = true;
	float gizmo_length = 2.5f;

	SceneState();
	void advance(double time);
	void sanitize();
	SceneUniform sceneUniform(float aspect) const;
	std::array<ObjectUniform, object_count> objectUniforms() const;
	void addPointLight();
	void addSpotLight();
	void removePointLight(size_t index);
	void removeSpotLight(size_t index);
};

}
