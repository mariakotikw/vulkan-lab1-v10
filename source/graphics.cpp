#include "graphics.hpp"

#include <cstring>
#include <stdexcept>

namespace graphics {

Buffer createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	Buffer buffer{};
	VmaAllocationInfo allocation{};
	if (vmaCreateBuffer(internal::context.allocator, &buffer_info, &allocation_info,
	                    &buffer.handle, &buffer.allocation, &allocation) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate a mapped buffer with VMA");
	}
	buffer.mapped = allocation.pMappedData;
	buffer.size = size;
	return buffer;
}

void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size) {
	if (size > buffer.size || buffer.mapped == nullptr) {
		throw std::runtime_error("Invalid mapped buffer write");
	}
	std::memcpy(buffer.mapped, data, static_cast<size_t>(size));
	// VMA handles noncoherent atom alignment; coherent memory needs no flush.
	if (vmaFlushAllocation(internal::context.allocator, buffer.allocation, 0, size) != VK_SUCCESS) {
		throw std::runtime_error("Failed to flush a mapped buffer");
	}
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.handle != VK_NULL_HANDLE) {
		vmaDestroyBuffer(internal::context.allocator, buffer.handle, buffer.allocation);
	}
	buffer = {};
}

} // namespace graphics
