#pragma once

#include "graphics_internal.hpp"

#include <functional>

namespace graphics {

struct Buffer {
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
	VkMemoryPropertyFlags memory_properties = 0;
};

Buffer createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
// Immutable mesh data is copied through a temporary host-visible staging buffer.
Buffer createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
void destroyBuffer(Buffer& buffer);

// Uses the graphics queue, waits on a fence, then releases upload command resources.
// Call from the same thread as rendering, while no other thread submits to this queue.
void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

}
