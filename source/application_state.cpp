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
	user_color = glm::clamp(user_color, glm::vec3(0.0f), glm::vec3(1.0f));
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
}

glm::mat4 ProjectionState::projectionMatrix(float aspect) const {
	aspect = std::max(aspect, 0.001f);
	glm::mat4 projection_matrix = projection == Projection::Perspective
		? glm::perspectiveRH_ZO(glm::radians(fov_degrees), aspect, 0.1f, 100.0f)
		: glm::orthoRH_ZO(-ortho_half_height * aspect, ortho_half_height * aspect,
		                  -ortho_half_height, ortho_half_height, 0.1f, 100.0f);
	projection_matrix[1][1] *= -1.0f;
	return projection_matrix;
}

SceneState::SceneState() {
	objects[0].transform.position = { -1.5f, 0.0f, 0.0f };
	objects[0].transform.scale = glm::vec3(0.6f);
	objects[0].user_color = { 1.0f, 0.55f, 0.35f };
	objects[1].transform.position = { 1.5f, 0.0f, 0.0f };
	objects[1].transform.rotation_degrees = { 15.0f, 35.0f, 5.0f };
	objects[1].transform.scale = glm::vec3(0.5f);
	objects[1].user_color = { 0.35f, 0.65f, 1.0f };
	for (auto& object : objects) object.initial_transform = object.transform;
}

void SceneState::advance(double time) {
	for (auto& object : objects) object.animation.advance(time);
}

void SceneState::sanitize() {
	camera.sanitize();
	for (auto& object : objects) object.sanitize();
}

std::array<TransformUniform, object_count> SceneState::uniforms(float aspect) const {
	const glm::mat4 view = glm::lookAtRH(glm::vec3(3.5f), glm::vec3(0), glm::vec3(0, 1, 0));
	const glm::mat4 view_projection = camera.projectionMatrix(aspect) * view;
	std::array<TransformUniform, object_count> result;
	for (size_t i = 0; i < objects.size(); ++i) {
		result[i] = { view_projection * objects[i].modelMatrix(), glm::vec4(objects[i].user_color, 1.0f) };
	}
	return result;
}

}
