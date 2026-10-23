#include "application_state.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace application {

void Animation::advance(double time) {
	if (clock_initialized && playing) {
		phase += std::max(0.0, time - last_time) * speed;
	}
	last_time = time;
	clock_initialized = true;
}

void Animation::reset() {
	phase = 0.0;
}

glm::vec3 Animation::offset() const {
	return { radius * std::sin(phase), vertical_amplitude * std::sin(2.0 * phase),
	         radius * (std::cos(phase) - 1.0) };
}

glm::vec3 Animation::rotation() const {
	return glm::vec3(0.45f, 0.7f, 0.3f) * static_cast<float>(phase);
}

void ObjectState::resetTransform() {
	transform = initial_transform;
}

void ObjectState::sanitize() {
	transform.scale = glm::clamp(transform.scale, glm::vec3(0.05f), glm::vec3(5.0f));
	animation.speed = std::clamp(animation.speed, 0.1f, 4.0f);
	animation.radius = std::clamp(animation.radius, 0.0f, 2.0f);
	animation.vertical_amplitude = std::clamp(animation.vertical_amplitude, 0.0f, 1.5f);
	material.sanitize();
}

glm::mat4 ObjectState::modelMatrix() const {
	const glm::vec3 rotation = glm::radians(transform.rotation_degrees) + animation.rotation();
	glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position + animation.offset());
	model = glm::rotate(model, rotation.x, glm::vec3(1, 0, 0));
	model = glm::rotate(model, rotation.y, glm::vec3(0, 1, 0));
	model = glm::rotate(model, rotation.z, glm::vec3(0, 0, 1));
	return glm::scale(model, glm::max(transform.scale, glm::vec3(0.05f)));
}

void ProjectionState::sanitize() {
	fov_degrees = std::clamp(fov_degrees, 15.0f, 90.0f);
	ortho_half_height = std::clamp(ortho_half_height, 1.0f, 8.0f);
	for (int i = 0; i < 3; ++i) if (!std::isfinite(position[i])) position[i] = 0;
	if (!std::isfinite(yaw_degrees)) yaw_degrees = -90;
	if (!std::isfinite(pitch_degrees)) pitch_degrees = 0;
	yaw_degrees = std::remainder(yaw_degrees, 360.0f);
	pitch_degrees = std::clamp(pitch_degrees, -89.0f, 89.0f);
	if (!std::isfinite(fov_degrees)) fov_degrees = 45;
	if (!std::isfinite(ortho_half_height)) ortho_half_height = 3.2f;
	near_plane = std::isfinite(near_plane) ? std::clamp(near_plane, 0.01f, 10.0f) : 0.1f;
	far_plane = std::isfinite(far_plane) ? std::clamp(far_plane, near_plane + 0.1f, 10000.0f) : 100.0f;
}

void ProjectionState::reset() { *this = ProjectionState{}; }
glm::vec3 ProjectionState::forward() const {
	const float yaw = glm::radians(yaw_degrees), pitch = glm::radians(pitch_degrees);
	return { std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch) };
}
glm::mat4 ProjectionState::viewMatrix() const {
	return glm::lookAtRH(position, position + forward(), glm::vec3(0, 1, 0));
}

glm::mat4 ProjectionState::projectionMatrix(float aspect) const {
	aspect = std::max(aspect, 0.001f);
	glm::mat4 projection_matrix = projection == Projection::Perspective
		? glm::perspectiveRH_ZO(glm::radians(fov_degrees), aspect, near_plane, far_plane)
		: glm::orthoRH_ZO(-ortho_half_height * aspect, ortho_half_height * aspect,
		                  -ortho_half_height, ortho_half_height, near_plane, far_plane);
	projection_matrix[1][1] *= -1.0f;
	return projection_matrix;
}

SceneState::SceneState() {
	objects[0].transform.position = { -1.5f, 0.0f, 0.0f };
	objects[0].transform.scale = glm::vec3(0.6f);
	objects[0].material.albedo = { 0.85f, 0.24f, 0.09f };
	objects[1].transform.position = { 1.5f, 0.0f, 0.0f };
	objects[1].transform.rotation_degrees = { 15.0f, 35.0f, 5.0f };
	objects[1].transform.scale = glm::vec3(0.5f);
	objects[1].material.albedo = { 0.08f, 0.38f, 0.85f };
	for (auto& object : objects) object.initial_transform = object.transform;
	point_lights.push_back({ { -2.8f, 1.5f, 1.8f }, { 1.0f, 0.35f, 0.12f }, 5.0f });
	point_lights.push_back({ { 2.8f, 1.0f, 1.5f }, { 0.15f, 0.45f, 1.0f }, 5.0f });
	SpotLight spot;
	spot.position = { 0, 3, 2 };
	spot.direction = safeDirection(glm::vec3(1.5f, 0, 0) - spot.position);
	spot.color = { 0.8f, 1.0f, 0.85f };
	spot.intensity = 18;
	spot_lights.push_back(spot);
}

void SceneState::advance(double time) {
	for (auto& object : objects) object.animation.advance(time);
}

void SceneState::sanitize() {
	camera.sanitize();
	for (auto& object : objects) object.sanitize();
	selected_object = std::clamp(selected_object, 0, static_cast<int>(object_count) - 1);
	ambient_color = glm::clamp(ambient_color, glm::vec3(0), glm::vec3(1));
	ambient_intensity = std::clamp(ambient_intensity, 0.0f, 2.0f);
	directional.sanitize();
	for (auto& light : point_lights) light.sanitize();
	for (auto& light : spot_lights) light.sanitize();
	gizmo_length = std::clamp(gizmo_length, 0.1f, 10.0f);
}

SceneUniform SceneState::sceneUniform(float aspect) const {
	return { camera.projectionMatrix(aspect) * camera.viewMatrix(), glm::vec4(camera.position, 1),
	         glm::vec4(ambient_color, ambient_intensity), glm::vec4(directional.direction, 0),
	         glm::vec4(directional.color, directional.intensity),
	         glm::uvec4(static_cast<uint32_t>(point_lights.size()), static_cast<uint32_t>(spot_lights.size()), 0, 0) };
}

std::array<ObjectUniform, object_count> SceneState::objectUniforms() const {
	std::array<ObjectUniform, object_count> result;
	for (size_t i = 0; i < objects.size(); ++i) {
		const auto model = objects[i].modelMatrix();
		const auto& material = objects[i].material;
		result[i] = { model, glm::transpose(glm::inverse(model)), glm::vec4(material.albedo, 1),
		              glm::vec4(material.specular, material.shininess) };
	}
	return result;
}

void SceneState::addPointLight() { point_lights.emplace_back(); }
void SceneState::addSpotLight() { spot_lights.emplace_back(); }
void SceneState::removePointLight(size_t index) {
	if (index < point_lights.size()) point_lights.erase(point_lights.begin() + index);
}
void SceneState::removeSpotLight(size_t index) {
	if (index < spot_lights.size()) spot_lights.erase(spot_lights.begin() + index);
}

}
