#include "texture_material.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace application {
namespace {
constexpr std::array<MaterialPreset, material_preset_count> presets{{
	{ "Cosmic Core", "assets/textures/cosmic_core_albedo.png",
	  "assets/textures/cosmic_core_specular.png", "assets/textures/cosmic_core_emissive.png" },
	{ "Nebula Mist", "assets/textures/nebula_mist_albedo.png",
	  "assets/textures/nebula_mist_specular.png", "assets/textures/nebula_mist_emissive.png" },
	{ "Holographic Prism", "assets/textures/holographic_prism_albedo.png",
	  "assets/textures/holographic_prism_specular.png", "assets/textures/holographic_prism_emissive.png" },
	{ "Prism Grid", "assets/textures/prism_grid_albedo.png", nullptr, nullptr },
}};
float finiteClamp(float value, float low, float high, float fallback) {
	return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
}

const std::array<MaterialPreset, material_preset_count>& materialPresets() { return presets; }

void HolographicEffect::sanitize() {
	speed = finiteClamp(speed, 0, 2, 0.35f);
	distortion = finiteClamp(distortion, 0, 0.08f, 0.018f);
	blend = finiteClamp(blend, 0, 1, 0.35f);
	color_modulation = finiteClamp(color_modulation, 0, 1, 0.22f);
}

void TextureSettings::sanitize() {
	material_preset = std::clamp(material_preset, 0, static_cast<int>(material_preset_count) - 1);
	emissive_strength = finiteClamp(emissive_strength, 0, 4, 1.4f);
	effect.sanitize();
}

TexturePushConstants TextureSettings::pushConstants(double time) const {
	TextureSettings valid = *this;
	valid.sanitize();
	// Use the renderer clock directly: object Play/Pause never freezes this.
	// Leave headroom for the shader's maximum speed to keep phase finite.
	const double max_time = static_cast<double>(std::numeric_limits<float>::max()) / 4.0;
	const float seconds = std::isfinite(time)
		? static_cast<float>(std::clamp(time, 0.0, max_time)) : 0.0f;
	return {
		{ seconds, valid.effect.speed, valid.effect.distortion, valid.effect.blend },
		{ valid.effect.enabled ? 1.0f : 0.0f, valid.effect.color_modulation,
		  valid.albedo_enabled ? 1.0f : 0.0f, valid.specular_enabled ? 1.0f : 0.0f },
		{ valid.emissive_enabled ? 1.0f : 0.0f, valid.emissive_strength, 0, 0 },
	};
}

size_t dynamicUniformStride(size_t object_size, size_t min_alignment) {
	if (!object_size) throw std::invalid_argument("Dynamic UBO object size must be nonzero");
	const size_t alignment = std::max(size_t{1}, min_alignment);
	if ((alignment & (alignment - 1)) != 0)
		throw std::invalid_argument("Dynamic UBO alignment must be a power of two");
	const size_t padding = alignment - 1;
	if (object_size > std::numeric_limits<size_t>::max() - padding)
		throw std::overflow_error("Dynamic UBO stride overflows size_t");
	return (object_size + padding) & ~padding;
}

uint32_t dynamicUniformOffset(size_t index, size_t stride) {
	if (!stride) throw std::invalid_argument("Dynamic UBO stride must be nonzero");
	if (index > std::numeric_limits<uint32_t>::max() / stride)
		throw std::overflow_error("Dynamic UBO offset exceeds uint32_t");
	return static_cast<uint32_t>(index * stride);
}

}
