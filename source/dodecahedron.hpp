#pragma once

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal{ 0.0f };
};

using Pentagon = std::array<uint32_t, 5>;

struct Dodecahedron {
	std::array<Vertex, 20> vertices;
	std::array<Pentagon, 12> faces;
	std::array<uint32_t, 108> indices;
};

Dodecahedron makeDodecahedron();

// Separate vertices per pentagon preserve sharp edges under interpolation.
struct FlatDodecahedron {
	std::array<Vertex, 60> vertices;
	std::array<uint32_t, 108> indices;
};
FlatDodecahedron makeFlatDodecahedron();

}
