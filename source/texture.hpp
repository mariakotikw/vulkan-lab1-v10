#pragma once

#include "graphics_internal.hpp"

namespace graphics {

struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkMemoryPropertyFlags memory_properties = 0;
};

// Albedo and emissive maps use sRGB; specular coefficient maps use linear UNORM.
Texture loadTexture(const char* path, bool srgb);
Texture createBlackTexture();
void destroyTexture(Texture& texture);

}
