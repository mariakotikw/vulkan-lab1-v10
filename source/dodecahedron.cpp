#include "dodecahedron.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace geometry {

Dodecahedron makeDodecahedron() {
	const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
	const float inv_phi = 1.0f / phi;
	const std::array<glm::vec3, 20> positions = {{
		{ -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, -1 }, { -1, 1, 1 },
		{ 1, -1, -1 }, { 1, -1, 1 }, { 1, 1, -1 }, { 1, 1, 1 },
		{ 0, -inv_phi, -phi }, { 0, -inv_phi, phi },
		{ 0, inv_phi, -phi }, { 0, inv_phi, phi },
		{ -inv_phi, -phi, 0 }, { -inv_phi, phi, 0 },
		{ inv_phi, -phi, 0 }, { inv_phi, phi, 0 },
		{ -phi, 0, -inv_phi }, { -phi, 0, inv_phi },
		{ phi, 0, -inv_phi }, { phi, 0, inv_phi },
	}};
	Dodecahedron mesh{};
	for (size_t i = 0; i < positions.size(); ++i) {
		const glm::vec3 color = (positions[i] / phi + glm::vec3(1.0f)) * 0.5f;
		mesh.vertices[i] = { positions[i], color };
	}
	mesh.faces = {{
		{ 0, 12, 1, 17, 16 }, { 0, 16, 2, 10, 8 },
		{ 0, 8, 4, 14, 12 }, { 1, 9, 11, 3, 17 },
		{ 1, 12, 14, 5, 9 }, { 2, 16, 17, 3, 13 },
		{ 2, 13, 15, 6, 10 }, { 3, 11, 7, 15, 13 },
		{ 4, 18, 19, 5, 14 }, { 4, 8, 10, 6, 18 },
		{ 5, 19, 7, 11, 9 }, { 6, 15, 7, 19, 18 },
	}};
	size_t index = 0;
	for (const auto& face : mesh.faces) {
		for (size_t i = 1; i < 4; ++i) {
			mesh.indices[index++] = face[0];
			mesh.indices[index++] = face[i];
			mesh.indices[index++] = face[i + 1];
		}
	}
	return mesh;
}

FlatDodecahedron makeFlatDodecahedron() {
	const auto topology = makeDodecahedron();
	FlatDodecahedron mesh{};
	size_t index = 0;
	for (size_t f = 0; f < topology.faces.size(); ++f) {
		const auto& face = topology.faces[f];
		const auto a = topology.vertices[face[0]].position;
		const auto normal = glm::normalize(glm::cross(topology.vertices[face[1]].position - a,
		                                              topology.vertices[face[2]].position - a));
		glm::vec3 center(0.0f);
		for (const auto v : face) center += topology.vertices[v].position;
		center /= static_cast<float>(face.size());
		// Keep the same readable vertical direction on each face. The fallback
		// makes the construction deterministic for faces parallel to the XZ plane.
		glm::vec3 up = glm::vec3(0, 1, 0) - normal * normal.y;
		if (glm::dot(up, up) < 1e-8f) up = glm::vec3(0, 0, 1) - normal * normal.z;
		if (glm::dot(up, up) < 1e-8f) up = glm::vec3(1, 0, 0) - normal * normal.x;
		up = glm::normalize(up);
		const glm::vec3 right = glm::normalize(glm::cross(up, normal));
		float radius = 0.0f;
		for (const auto v : face)
			radius = std::max(radius, glm::length(topology.vertices[v].position - center));
		// One isotropic scale preserves lengths and angles, with a 4% margin.
		const float uv_scale = 0.46f / radius;
		for (size_t v = 0; v < face.size(); ++v) {
			mesh.vertices[f * 5 + v] = topology.vertices[face[v]];
			mesh.vertices[f * 5 + v].normal = normal;
			const glm::vec3 local = topology.vertices[face[v]].position - center;
			// PNG rows are uploaded top-to-bottom: decreasing V points upward.
			mesh.vertices[f * 5 + v].uv = glm::vec2(0.5f) + uv_scale *
				glm::vec2(glm::dot(local, right), -glm::dot(local, up));
		}
		for (uint32_t t = 1; t < 4; ++t) {
			mesh.indices[index++] = static_cast<uint32_t>(f * 5);
			mesh.indices[index++] = static_cast<uint32_t>(f * 5) + t;
			mesh.indices[index++] = static_cast<uint32_t>(f * 5) + t + 1;
		}
	}
	return mesh;
}

}
