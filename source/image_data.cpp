#include "image_data.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_WINDOWS_UTF8
#define STBI_MAX_DIMENSIONS 8192
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../third_party/stb_image.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace graphics {

ImageData loadImageRgba(const char* path) {
	if (path == nullptr || *path == '\0') throw std::runtime_error("Empty texture path");
	int width = 0, height = 0, channels = 0;
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
		stbi_load(path, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
	if (!decoded) {
		const char* reason = stbi_failure_reason();
		throw std::runtime_error(std::string("Cannot decode texture '") + path + "': " +
		                         (reason ? reason : "unknown PNG decoding error"));
	}
	const uint64_t byte_count = uint64_t(width) * uint64_t(height) * 4;
	if (width <= 0 || height <= 0 || byte_count > std::numeric_limits<size_t>::max()) {
		throw std::runtime_error(std::string("Invalid texture dimensions: ") + path);
	}
	ImageData image{ .width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height) };
	image.pixels.assign(decoded.get(), decoded.get() + static_cast<size_t>(byte_count));
	return image;
}

}
