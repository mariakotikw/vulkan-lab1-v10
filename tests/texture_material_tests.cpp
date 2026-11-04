#include "dodecahedron.hpp"
#include "texture_material.hpp"

#include <glm/geometric.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
constexpr float tolerance = 0.0001f;
void require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < tolerance; }
bool close(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < tolerance; }
template<class Exception, class Callback>
void requireThrows(Callback callback, const char* message) {
	try { callback(); }
	catch (const Exception&) { return; }
	throw std::runtime_error(message);
}
float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

void checkFaceUVs() {
	const auto topology = geometry::makeDodecahedron();
	const auto mesh = geometry::makeFlatDodecahedron();
	require(topology.vertices.size() == 20 && topology.faces.size() == 12 && topology.indices.size() == 108,
	        "Canonical lab 1 topology changed");
	require(mesh.vertices.size() == 60 && mesh.indices.size() == 108,
	        "Flat mesh must have five private vertices per face");
	float common_uv_edge = 0;
	for (size_t f = 0; f < topology.faces.size(); ++f) {
		const auto& face = topology.faces[f];
		const glm::vec3 normal = mesh.vertices[f * 5].normal;
		require(close(glm::length(normal), 1), "Face normal is not unit length");
		glm::vec3 center(0);
		glm::vec2 uv_center(0);
		for (size_t v = 0; v < 5; ++v) {
			center += mesh.vertices[f * 5 + v].position;
			uv_center += mesh.vertices[f * 5 + v].uv;
		}
		center /= 5.0f;
		uv_center /= 5.0f;
		require(glm::dot(normal, center) > 0, "Face normal points inward");
		require(glm::length(uv_center - glm::vec2(0.5f)) < tolerance, "Face UV is not centered");
		const glm::vec3 projected_up = glm::vec3(0, 1, 0) - normal * normal.y;
		const glm::vec3 up = glm::normalize(projected_up);
		const glm::vec3 right = glm::normalize(glm::cross(up, normal));
		float scale = 0;
		for (size_t v = 0; v < 5; ++v) {
			const auto& vertex = mesh.vertices[f * 5 + v];
			const auto& original = topology.vertices[face[v]];
			require(close(vertex.position, original.position) && close(vertex.color, original.color),
			        "UV generation changed original vertex positions/colors");
			require(close(vertex.normal, normal), "Normal is interpolated across a pentagon");
			require(close(glm::dot(normal, vertex.position - center), 0), "Face projection is not planar");
			require(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y), "UV coordinate is not finite");
			for (int component = 0; component < 2; ++component)
				require(vertex.uv[component] >= 0.04f - tolerance && vertex.uv[component] <= 0.96f + tolerance,
				        "UV is outside the safe image margin");
			require(close(glm::length(vertex.uv - glm::vec2(0.5f)), 0.46f),
			        "Pentagon UV does not use a common circumradius");
			for (size_t w = v + 1; w < 5; ++w) {
				const auto& other = mesh.vertices[f * 5 + w];
				const glm::vec3 edge = other.position - vertex.position;
				const glm::vec2 uv_edge = other.uv - vertex.uv;
				const float this_scale = glm::length(uv_edge) / glm::length(edge);
				if (!scale) scale = this_scale;
				require(close(this_scale, scale), "UV projection stretches lengths unequally");
				require(close(uv_edge.x, scale * glm::dot(edge, right)), "Face texture is mirrored horizontally");
				require(close(uv_edge.y, -scale * glm::dot(edge, up)), "Top-left PNG orientation is inverted");
			}
			const float edge_length = glm::length(mesh.vertices[f * 5 + (v + 1) % 5].uv - vertex.uv);
			if (!common_uv_edge) common_uv_edge = edge_length;
			require(close(edge_length, common_uv_edge), "Pentagon UV edge lengths differ between faces");
		}
		float area = 0;
		for (size_t t = 0; t < 3; ++t) {
			const size_t tri = f * 9 + t * 3;
			for (size_t corner = 0; corner < 3; ++corner) {
				const auto flat_index = mesh.indices[tri + corner];
				require(flat_index / 5 == f, "Triangle refers to another face's UV vertices");
				require(close(mesh.vertices[flat_index].position,
				              topology.vertices[topology.indices[tri + corner]].position),
				        "Flat mesh triangulation changed the canonical triangles");
			}
			const auto& a = mesh.vertices[mesh.indices[tri]];
			const auto& b = mesh.vertices[mesh.indices[tri + 1]];
			const auto& c = mesh.vertices[mesh.indices[tri + 2]];
			require(glm::dot(glm::cross(b.position - a.position, c.position - a.position), normal) > tolerance,
			        "UV generation changed geometry winding");
			const float uv_cross = cross2(b.uv - a.uv, c.uv - a.uv);
			// The one consistent V inversion is required by PNG's top-left origin.
			require(uv_cross < -tolerance, "Face UV triangle is degenerate or unexpectedly flipped");
			area -= uv_cross / 2;
		}
		const float expected_area = 2.5f * 0.46f * 0.46f * std::sin(2.0f * std::acos(-1.0f) / 5.0f);
		require(close(area, expected_area), "UV triangles do not cover a complete regular pentagon");
	}
}

void checkDynamicUniforms() {
	using application::dynamicUniformOffset;
	using application::dynamicUniformStride;
	for (const size_t alignment : {size_t{0}, size_t{1}, size_t{64}, size_t{256}, size_t{512}}) {
		for (const size_t size : {size_t{1}, size_t{48}, size_t{176}, size_t{256}, size_t{513}}) {
			const size_t stride = dynamicUniformStride(size, alignment);
			const size_t effective_alignment = alignment ? alignment : 1;
			require(stride >= size && stride - size < effective_alignment, "Dynamic UBO stride has excess/insufficient padding");
			require(stride % effective_alignment == 0, "Dynamic UBO stride violates device alignment");
			for (size_t object = 0; object < 3; ++object)
				require(dynamicUniformOffset(object, stride) == object * stride, "Dynamic UBO object offset is incorrect");
		}
	}
	require(dynamicUniformStride(256, 256) == 256, "Already aligned UBO gained a full padding block");
	require(dynamicUniformStride(176, 256) == 256, "std140 ObjectUniform stride was not padded");
	requireThrows<std::invalid_argument>([] { dynamicUniformStride(0, 256); }, "Zero object size accepted");
	requireThrows<std::invalid_argument>([] { dynamicUniformStride(48, 3); }, "Non-power-of-two alignment accepted");
	requireThrows<std::invalid_argument>([] { dynamicUniformOffset(0, 0); }, "Zero stride accepted");
	const size_t max_size = std::numeric_limits<size_t>::max();
	require(dynamicUniformStride(max_size, 1) == max_size, "Unpadded maximal stride rejected");
	requireThrows<std::overflow_error>([=] { dynamicUniformStride(max_size, 256); }, "Stride overflow was not rejected");
	const auto max_offset = std::numeric_limits<uint32_t>::max();
	require(dynamicUniformOffset(max_offset, 1) == max_offset, "Largest Vulkan dynamic offset was rejected");
	requireThrows<std::overflow_error>([=] { dynamicUniformOffset(max_offset, 2); }, "Vulkan uint32_t offset overflow was accepted");
	requireThrows<std::overflow_error>([=] { dynamicUniformOffset(max_size, max_size); }, "Native multiply overflow was accepted");
	if constexpr (sizeof(size_t) > sizeof(uint32_t)) {
		requireThrows<std::overflow_error>([=] { dynamicUniformOffset(size_t{1} + max_offset, 1); }, "64-bit index silently narrowed");
	}
}

void checkTextureSettings() {
	using namespace application;
	const auto& presets = materialPresets();
	require(presets.size() == 4 && material_preset_count == 4, "Unexpected material preset count");
	const std::array<std::string_view, 4> names{{ "Cosmic Core", "Nebula Mist", "Holographic Prism", "Prism Grid" }};
	for (size_t i = 0; i < presets.size(); ++i) {
		require(presets[i].name == names[i] && presets[i].albedo_path, "Preset order/name/albedo is incorrect");
		require(std::string_view(presets[i].albedo_path).ends_with("_albedo.png"), "Preset does not load an albedo PNG");
		if (i < 3) require(presets[i].specular_path && presets[i].emissive_path, "Complete material lost optional maps");
	}
	require(!presets[3].specular_path && !presets[3].emissive_path,
	        "Albedo-only preset cannot exercise the black missing-map fallback");
	TextureSettings settings;
	require(settings.material_preset == 0 && settings.albedo_enabled && settings.specular_enabled && settings.emissive_enabled,
	        "Default texture settings changed");
	require(!settings.effect.enabled && close(settings.emissive_strength, 1.4f), "Default effect/emission settings changed");
	const auto a = settings.pushConstants(12.5);
	std::array<float, 12> packed{};
	std::memcpy(packed.data(), &a, sizeof(a));
	const std::array<float, 12> expected{{12.5f, .35f, .018f, .35f, 0, .22f, 1, 1, 1, 1.4f, 0, 0}};
	for (size_t i = 0; i < packed.size(); ++i) require(close(packed[i], expected[i]), "Push-constant layout/data differs from shader ABI");
	settings.effect.enabled = true;
	settings.albedo_enabled = settings.specular_enabled = settings.emissive_enabled = false;
	const auto b = settings.pushConstants(99.25);
	require(close(b.animation.x, 99.25f) && close(b.controls.x, 1) && close(b.controls.z, 0)
	        && close(b.controls.w, 0) && close(b.emission.x, 0), "Effect/map toggles do not reach push constants");
	// No object animation phase is an input: a running texture clock continues
	// while the owner leaves all geometric animation fields untouched.
	const auto later = settings.pushConstants(100.25);
	require(close(later.animation.x - b.animation.x, 1) && close(later.animation.y, b.animation.y),
	        "Texture effect clock does not advance independently");
	TextureSettings other;
	other.material_preset = 2;
	other.effect.speed = 1.5f;
	require(close(settings.pushConstants(1).animation.y, .35f) && close(other.pushConstants(1).animation.y, 1.5f),
	        "Per-object texture settings share mutable state");
	settings.material_preset = -10;
	settings.emissive_strength = -1;
	settings.effect.speed = -1;
	settings.effect.distortion = 1;
	settings.effect.blend = 2;
	settings.effect.color_modulation = -2;
	settings.sanitize();
	require(settings.material_preset == 0 && close(settings.emissive_strength, 0)
	        && close(settings.effect.speed, 0) && close(settings.effect.distortion, .08f)
	        && close(settings.effect.blend, 1) && close(settings.effect.color_modulation, 0), "Unsafe effect values were not clamped");
	settings.material_preset = 99;
	settings.emissive_strength = std::numeric_limits<float>::infinity();
	settings.effect.speed = std::numeric_limits<float>::quiet_NaN();
	settings.effect.distortion = settings.effect.blend = settings.effect.color_modulation = settings.effect.speed;
	const auto safe = settings.pushConstants(std::numeric_limits<double>::infinity());
	require(close(safe.animation.x, 0) && close(safe.animation.y, .35f) && close(safe.animation.z, .018f)
	        && close(safe.animation.w, .35f) && close(safe.controls.y, .22f) && close(safe.emission.y, 1.4f),
	        "Nonfinite values escaped into push constants");
	require(settings.material_preset == 99 && std::isnan(settings.effect.speed), "Const packing modified source settings");
	settings.sanitize();
	require(settings.material_preset == 3 && close(settings.emissive_strength, 1.4f), "Preset/nonfinite sanitization failed");
	require(close(settings.pushConstants(-100).animation.x, 0)
	        && close(settings.pushConstants(std::numeric_limits<double>::quiet_NaN()).animation.x, 0), "Invalid clock input accepted");
	require(std::isfinite(settings.pushConstants(std::numeric_limits<double>::max()).animation.x * 2),
	        "Large clock produces an infinite shader phase");
}
}

int main() {
	try {
		checkFaceUVs();
		checkDynamicUniforms();
		checkTextureSettings();
		std::cout << "PASS: 12 planar/isotropic/readable pentagon UVs; preserved topology and normals; "
		             "dynamic UBO alignment/overflow; 48-byte push ABI; materials/toggles/independent texture clock/sanitization\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
