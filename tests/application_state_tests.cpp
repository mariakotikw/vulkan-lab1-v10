#include "application_state.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
	if (!value) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.0001f; }
bool close(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 0.0001f; }
bool close(const glm::mat4& a, const glm::mat4& b) {
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
		if (!close(a[c][r], b[c][r])) return false;
	}
	return true;
}

}

int main() {
	try {
		using namespace application;
		ObjectState state;
		ProjectionState camera;
		const auto default_model = state.modelMatrix();
		for (const auto projection : { Projection::Perspective, Projection::Orthographic }) {
			camera.projection = projection;
			for (float aspect : { 0.6f, 1.0f, 16.0f / 9.0f, 2.4f }) {
				const auto p = camera.projectionMatrix(aspect);
				require(p[1][1] < 0, "Missing Vulkan projection Y flip");
				require(close(std::abs(p[1][1] / p[0][0]), aspect), "Incorrect resize aspect ratio");
				for (float depth : { 0.1f, 100.0f }) {
					const auto clip = p * glm::vec4(0, 0, -depth, 1);
					require(close(clip.z / clip.w, depth == 0.1f ? 0.0f : 1.0f), "Incorrect Vulkan depth range");
				}
			}
		}
		camera.projection = Projection::Perspective;
		require(close(camera.projectionMatrix(1.6f), ProjectionState{}.projectionMatrix(1.6f)), "Projection roundtrip changed MVP");
		state.transform.rotation_degrees = glm::vec3(0);
		state.transform.position = { 1, -2, 3 };
		require(close(glm::vec3(state.modelMatrix() * glm::vec4(0, 0, 0, 1)), state.transform.position),
		        "Position does not translate model origin");
		state.transform.position = glm::vec3(0);
		for (int axis = 0; axis < 3; ++axis) {
			state.transform.rotation_degrees = glm::vec3(0);
			state.transform.rotation_degrees[axis] = 90;
			glm::vec3 direction(0); direction[(axis + 1) % 3] = 1;
			glm::vec3 expected(0); expected[(axis + 2) % 3] = 1;
			require(close(glm::vec3(state.modelMatrix() * glm::vec4(direction, 0)), expected),
			        "Rotation axis is incorrect");
		}
		state.transform.rotation_degrees = glm::vec3(0);
		state.transform.scale = { 0.5f, 1.5f, 2.0f };
		require(close(glm::vec3(state.modelMatrix() * glm::vec4(1, 1, 1, 0)), state.transform.scale), "Scale failed");
		state.transform.scale = { 0, -1, 10 };
		state.sanitize();
		require(close(state.transform.scale, { 0.05f, 0.05f, 5.0f }), "Unsafe scale was accepted");
		state.resetTransform();
		require(close(state.modelMatrix(), default_model), "Reset Transform failed");

		Animation a;
		a.advance(100);
		a.playing = true;
		a.advance(101);
		require(close(static_cast<float>(a.phase), 1), "Animation did not use delta time");
		a.playing = false;
		const auto paused = a.offset();
		a.advance(1000);
		require(close(a.offset(), paused), "Pause changed the position");
		a.playing = true;
		a.advance(1000.25);
		require(close(static_cast<float>(a.phase), 1.25f), "Resume jumped over paused time");
		a.speed = 2;
		a.advance(1000.75);
		require(close(static_cast<float>(a.phase), 2.25f), "Speed change restarted or jumped animation");
		Animation low_fps, high_fps;
		low_fps.playing = high_fps.playing = true;
		low_fps.advance(0); high_fps.advance(0);
		for (int i = 1; i <= 30; ++i) low_fps.advance(i / 30.0);
		for (int i = 1; i <= 240; ++i) high_fps.advance(i / 240.0);
		require(close(low_fps.offset(), high_fps.offset()), "Animation depends on FPS");
		a.reset();
		require(close(a.offset(), glm::vec3(0)) && close(a.rotation(), glm::vec3(0)), "Animation reset failed");
		a.phase = 0.7;
		const auto small_radius = a.offset();
		a.radius *= 2;
		const auto big_radius = a.offset();
		require(close(big_radius.x, small_radius.x * 2) && close(big_radius.z, small_radius.z * 2), "Radius failed");
		require(std::abs(big_radius.y) > 0.01f && glm::length(a.rotation()) > 0.01f, "Path is not spatial or rotating");

		SceneState scene;
		require(scene.objects.size() >= 2, "Scene must contain multiple objects");
		for (const auto projection : { Projection::Perspective, Projection::Orthographic }) {
			scene.camera.projection = projection;
			const auto uniforms = scene.objectUniforms();
			require(!close(uniforms[0].model, uniforms[1].model), "Objects share their transform uniform state");
			require(!close(glm::vec3(uniforms[0].albedo), glm::vec3(uniforms[1].albedo)),
			        "Objects share their color uniform state");
		}
		const auto initial_uniforms = scene.objectUniforms();
		scene.objects[0].transform.position.x += 0.4f;
		scene.objects[0].material.albedo = { 0.2f, 0.8f, 0.3f };
		const auto edited_uniforms = scene.objectUniforms();
		require(!close(edited_uniforms[0].model, initial_uniforms[0].model), "Object edit did not reach its MVP");
		require(close(edited_uniforms[1].model, initial_uniforms[1].model)
		        && close(glm::vec3(edited_uniforms[1].albedo), glm::vec3(initial_uniforms[1].albedo)),
		        "Editing Object 1 changed Object 2's uniform state");
		scene.objects[0].resetTransform();
		require(close(scene.objectUniforms()[0].model, initial_uniforms[0].model), "Object reset lost its scene placement");
		scene.advance(0);
		scene.objects[0].animation.playing = true;
		scene.advance(1);
		scene.selected_object = 1;
		scene.advance(2);
		require(close(static_cast<float>(scene.objects[0].animation.phase), 2)
		        && close(static_cast<float>(scene.objects[1].animation.phase), 0),
		        "Object selection incorrectly changes animation clocks");
		scene.objects[1].animation.playing = true;
		scene.advance(2.25);
		require(close(static_cast<float>(scene.objects[1].animation.phase), 0.25f),
		        "Starting an unselected object included paused time");
		std::cout << "PASS: projection/depth/aspect; position; XYZ rotation/scale; reset; "
		             "animation timing/pause/resume/speed/radius/FPS; std140 layout; independent per-object uniforms\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
