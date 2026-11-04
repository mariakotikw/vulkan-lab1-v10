#pragma once

#include <cstdint>
#include <vector>

namespace graphics {

// PNG rows retain their file order; UV v=0 addresses the image's top row.
struct ImageData {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> pixels;
};

// Decodes a real file to tightly packed RGBA8; throws with the path on failure.
ImageData loadImageRgba(const char* path);

}
