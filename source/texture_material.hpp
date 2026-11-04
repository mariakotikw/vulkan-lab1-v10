#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <glm/vec4.hpp>

namespace application {

inline constexpr size_t material_preset_count = 4;
struct MaterialPreset {
	const char* name;
	const char* albedo_path;
	const char* specular_path;
	const char* emissive_path;
};
const std::array<MaterialPreset, material_preset_count>& materialPresets();

struct HolographicEffect {
	bool enabled = false;
	float speed = 0.35f;
	float distortion = 0.018f;
	float blend = 0.35f;
	float color_modulation = 0.22f;
	void sanitize();
};

// Three vec4s match the fragment shader push-constant block, independent of
// ObjectUniform and its dynamic-UBO alignment requirements.
struct alignas(16) TexturePushConstants {
	glm::vec4 animation;
	glm::vec4 controls;
	glm::vec4 emission;
};
static_assert(std::is_standard_layout_v<TexturePushConstants>);
static_assert(alignof(TexturePushConstants) == 16);
static_assert(offsetof(TexturePushConstants, animation) == 0);
static_assert(offsetof(TexturePushConstants, controls) == 16);
static_assert(offsetof(TexturePushConstants, emission) == 32);
static_assert(sizeof(TexturePushConstants) == 48);

struct TextureSettings {
	int material_preset = 0;
	bool albedo_enabled = true;
	bool specular_enabled = true;
	bool emissive_enabled = true;
	float emissive_strength = 1.4f;
	HolographicEffect effect;
	void sanitize();
	TexturePushConstants pushConstants(double time) const;
};

// Vulkan requires a power-of-two minUniformBufferOffsetAlignment. Zero/one
// both mean no padding. Invalid sizes/alignments and overflow throw.
size_t dynamicUniformStride(size_t object_size, size_t min_alignment);
// Vulkan dynamic descriptor offsets use uint32_t even on a 64-bit host.
uint32_t dynamicUniformOffset(size_t index, size_t stride);

}
