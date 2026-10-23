#pragma once
#include "application_state.hpp"
#include <vector>

namespace application {
struct GizmoVertex { glm::vec3 position; glm::vec3 color; };
struct LightGizmos {
	std::vector<GizmoVertex> triangles;
	std::vector<GizmoVertex> lines;
};
LightGizmos makeLightGizmos(const SceneState& scene);
}
