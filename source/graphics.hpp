#pragma once

#include "graphics_internal.hpp"

namespace graphics {

// Explicitly owned by application; destroy before the starter's VMA allocator.
struct Buffer {
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
};

Buffer createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size);
void destroyBuffer(Buffer& buffer);

} // namespace graphics
