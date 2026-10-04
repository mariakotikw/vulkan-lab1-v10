#include "dodecahedron.hpp"

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {

constexpr float tolerance = 0.0001f;
void require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < tolerance; }

} // namespace

int main() {
	try {
		const auto mesh = geometry::makeDodecahedron();
		require(mesh.vertices.size() == 20 && mesh.faces.size() == 12 && mesh.indices.size() == 108,
		        "Incorrect vertex/face/index count");
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			const auto position = mesh.vertices[i].position;
			require(close(glm::dot(position, position), 3), "Vertices are not on a common sphere");
			for (size_t j = 0; j < i; ++j) {
				require(glm::length(position - mesh.vertices[j].position) > tolerance, "Duplicate vertex");
			}
		}
		const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
		const float edge_length = 2.0f / phi;
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			const auto& vertex = mesh.vertices[i];
			for (int channel = 0; channel < 3; ++channel) {
				require(vertex.color[channel] >= 0 && vertex.color[channel] <= 1, "Vertex color outside RGB bounds");
				require(close(vertex.color[channel], 0.5f + vertex.position[channel] / (2.0f * phi)),
				        "Vertex color does not follow local coordinates");
			}
			for (size_t j = 0; j < i; ++j) {
				require(glm::length(vertex.color - mesh.vertices[j].color) > tolerance, "Procedural colors are not distinct");
			}
		}
		std::map<std::pair<uint32_t, uint32_t>, unsigned> edge_uses;
		std::map<std::pair<uint32_t, uint32_t>, int> edge_orientations;
		std::set<std::array<uint32_t, 5>> unique_faces;
		float polygon_area = 0;
		for (const auto& face : mesh.faces) {
			auto sorted_face = face;
			std::sort(sorted_face.begin(), sorted_face.end());
			require(sorted_face.back() < mesh.vertices.size(), "Face index out of bounds");
			require(std::adjacent_find(sorted_face.begin(), sorted_face.end()) == sorted_face.end(),
			        "Repeated vertex in a face");
			require(unique_faces.insert(sorted_face).second, "Duplicate pentagon");
			const auto a = mesh.vertices[face[0]].position;
			const auto normal = glm::normalize(glm::cross(mesh.vertices[face[1]].position - a,
			                                              mesh.vertices[face[2]].position - a));
			require(glm::dot(normal, a) > 0, "Pentagon winding is inward");
			for (const auto& vertex : mesh.vertices) {
				require(glm::dot(normal, vertex.position - a) < tolerance, "Face is not a convex hull plane");
			}
			for (size_t i = 0; i < face.size(); ++i) {
				const auto u = face[i], v = face[(i + 1) % 5];
				const auto p = mesh.vertices[u].position;
				const auto q = mesh.vertices[v].position;
				const auto r = mesh.vertices[face[(i + 2) % 5]].position;
				require(close(glm::dot(normal, p - a), 0), "Pentagon is not planar");
				require(close(glm::length(q - p), edge_length), "Pentagon edges are not equal");
				// The interior angle of a regular pentagon is 108 degrees.
				require(close(glm::dot(glm::normalize(p - q), glm::normalize(r - q)),
				              std::cos(108.0f * std::acos(-1.0f) / 180.0f)), "Pentagon angles are not equal");
				const auto edge = std::make_pair(std::min(u, v), std::max(u, v));
				++edge_uses[edge];
				edge_orientations[edge] += u < v ? 1 : -1;
				polygon_area += 0.5f * glm::dot(normal, glm::cross(p, q));
			}
		}
		require(edge_uses.size() == 30, "A dodecahedron must have 30 edges");
		for (const auto& [edge, uses] : edge_uses) {
			require(uses == 2 && edge_orientations[edge] == 0, "Mesh has a hole or inconsistent winding");
		}
		float triangle_area = 0;
		for (size_t i = 0; i < mesh.indices.size(); i += 3) {
			for (size_t j = 0; j < 3; ++j) {
				require(mesh.indices[i + j] < mesh.vertices.size(), "Triangle index out of bounds");
			}
			const auto a = mesh.vertices[mesh.indices[i]].position;
			const auto b = mesh.vertices[mesh.indices[i + 1]].position;
			const auto c = mesh.vertices[mesh.indices[i + 2]].position;
			const auto normal = glm::cross(b - a, c - a);
			require(glm::dot(normal, a) > tolerance, "Triangle is degenerate or faces inward");
			for (const auto& vertex : mesh.vertices) {
				require(glm::dot(normal, vertex.position - a) < tolerance, "Triangle cuts through the solid");
			}
			triangle_area += glm::length(normal) / 2.0f;
		}
		require(close(triangle_area, polygon_area), "Triangulation does not cover the complete surface");
		std::cout << "PASS: 20 unique vertices, 12 regular pentagons, 30 shared edges, "
		             "36 outward triangles; closed convex surface; 20 distinct procedural RGB colors\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
