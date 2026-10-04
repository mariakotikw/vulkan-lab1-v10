#include "dodecahedron.hpp"

#include <cmath>

namespace geometry {

Dodecahedron makeDodecahedron() {
	const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
	const float inv_phi = 1.0f / phi;
	// (+/-1, +/-1, +/-1), (0, +/-1/phi, +/-phi),
	// (+/-1/phi, +/-phi, 0), (+/-phi, 0, +/-1/phi).
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
		// Map the local coordinate bounds [-phi, phi] to RGB [0,1].
		const glm::vec3 color = (positions[i] / phi + glm::vec3(1.0f)) * 0.5f;
		mesh.vertices[i] = { positions[i], color };
	}
	// Counterclockwise when each pentagon is viewed from outside the solid.
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
		// A convex pentagon is a fan of three outward-facing triangles.
		for (size_t i = 1; i < 4; ++i) {
			mesh.indices[index++] = face[0];
			mesh.indices[index++] = face[i];
			mesh.indices[index++] = face[i + 1];
		}
	}
	return mesh;
}

} // namespace geometry
