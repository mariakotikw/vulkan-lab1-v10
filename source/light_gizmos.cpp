#include "light_gizmos.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace application {
namespace {
void line(LightGizmos& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 color) {
	mesh.lines.push_back({ a, color });
	mesh.lines.push_back({ b, color });
}
void marker(LightGizmos& mesh, const PointLight& light) {
	constexpr glm::vec3 corners[] = {
		{ -1,-1,-1 }, { 1,-1,-1 }, { 1,1,-1 }, { -1,1,-1 },
		{ -1,-1,1 }, { 1,-1,1 }, { 1,1,1 }, { -1,1,1 }
	};
	constexpr unsigned indices[] = {
		0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
		3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5
	};
	for (unsigned i : indices) mesh.triangles.push_back({ light.position + 0.075f * corners[i], light.color });
}
}
LightGizmos makeLightGizmos(const SceneState& scene) {
	LightGizmos mesh;
	if (!scene.show_light_gizmos) return mesh;
	for (const auto& light : scene.point_lights) marker(mesh, light);
	for (auto light : scene.spot_lights) {
		light.sanitize();
		marker(mesh, light);
		const auto d = light.direction;
		const auto u = safeDirection(glm::cross(d, std::abs(d.y) < 0.95f ? glm::vec3(0,1,0) : glm::vec3(1,0,0)));
		const auto v = glm::cross(d, u);
		const auto end = light.position + d * scene.gizmo_length;
		line(mesh, light.position, end, light.color);
		for (float angle : { light.inner_degrees, light.outer_degrees }) {
			const float radius = scene.gizmo_length * std::tan(glm::radians(angle));
			for (int segment = 0; segment < 48; ++segment) {
				const float a = glm::two_pi<float>() * segment / 48.0f;
				const float b = glm::two_pi<float>() * (segment + 1) / 48.0f;
				const auto p = end + radius * (std::cos(a) * u + std::sin(a) * v);
				const auto q = end + radius * (std::cos(b) * u + std::sin(b) * v);
				line(mesh, p, q, glm::vec3(1));
				if (segment % 12 == 0) line(mesh, light.position, p, glm::vec3(1));
			}
		}
	}
	return mesh;
}
}
