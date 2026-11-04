#include "graphics.hpp"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace graphics {

Buffer createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
	if (size == 0) throw std::runtime_error("Cannot allocate an empty buffer");
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
		.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
	};
	Buffer buffer{};
	VmaAllocationInfo allocation{};
	if (vmaCreateBuffer(internal::context.allocator, &buffer_info, &allocation_info,
	                    &buffer.handle, &buffer.allocation, &allocation) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate a mapped buffer with VMA");
	}
	buffer.mapped = allocation.pMappedData;
	buffer.size = size;
	vmaGetAllocationMemoryProperties(internal::context.allocator, buffer.allocation,
	                                 &buffer.memory_properties);
	if (buffer.mapped == nullptr) {
		destroyBuffer(buffer);
		throw std::runtime_error("VMA mapped allocation did not expose host memory");
	}
	return buffer;
}

void writeBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
	if (offset > buffer.size || size > buffer.size - offset || buffer.mapped == nullptr ||
	    (size != 0 && data == nullptr)) {
		throw std::runtime_error("Invalid mapped buffer write");
	}
	if (size == 0) return;
	std::memcpy(static_cast<unsigned char*>(buffer.mapped) + offset, data, static_cast<size_t>(size));
	if (vmaFlushAllocation(internal::context.allocator, buffer.allocation, offset, size) != VK_SUCCESS) {
		throw std::runtime_error("Failed to flush a mapped buffer");
	}
}

void immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
	const auto device = internal::context.device;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	bool submitted = false;
	auto require = [](VkResult result, const char* operation) {
		if (result != VK_SUCCESS) {
			throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(result) + ")");
		}
	};
	try {
		const VkCommandPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = internal::context.graphics_queue_index,
		};
		require(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "Create upload command pool");
		const VkCommandBufferAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		VkCommandBuffer command = VK_NULL_HANDLE;
		require(vkAllocateCommandBuffers(device, &allocate_info, &command), "Allocate upload command buffer");
		const VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		require(vkBeginCommandBuffer(command, &begin_info), "Begin upload command buffer");
		record(command);
		require(vkEndCommandBuffer(command), "End upload command buffer");
		const VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		require(vkCreateFence(device, &fence_info, nullptr, &fence), "Create upload fence");
		const VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &command,
		};
		require(vkQueueSubmit(internal::context.graphics_queue, 1, &submit_info, fence), "Submit resource upload");
		submitted = true;
		require(vkWaitForFences(device, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()), "Wait for resource upload");
		submitted = false;
	} catch (...) {
		// Never release a command pool while its submitted command buffer is in use.
		if (submitted) vkQueueWaitIdle(internal::context.graphics_queue);
		if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
		if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
		throw;
	}
	vkDestroyFence(device, fence, nullptr);
	vkDestroyCommandPool(device, pool, nullptr);
}

Buffer createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
	if (data == nullptr || size == 0) throw std::runtime_error("Cannot upload empty mesh data");
	Buffer staging{};
	Buffer destination{};
	try {
		staging = createMappedBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		writeBuffer(staging, data, size);
		const VkBufferCreateInfo buffer_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		const VmaAllocationCreateInfo allocation_info = {
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};
		if (vmaCreateBuffer(internal::context.allocator, &buffer_info, &allocation_info,
		                    &destination.handle, &destination.allocation, nullptr) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate device-local mesh buffer");
		}
		destination.size = size;
		vmaGetAllocationMemoryProperties(internal::context.allocator, destination.allocation,
		                                 &destination.memory_properties);
		if ((destination.memory_properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) {
			throw std::runtime_error("Mesh allocation does not use device-local memory");
		}
		immediateSubmit([&](VkCommandBuffer command) {
			const VkBufferCopy copy = { .size = size };
			vkCmdCopyBuffer(command, staging.handle, destination.handle, 1, &copy);
			VkAccessFlags consumer_access = 0;
			if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) consumer_access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) consumer_access |= VK_ACCESS_INDEX_READ_BIT;
			const bool mesh = consumer_access != 0;
			const VkBufferMemoryBarrier visible = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = mesh ? consumer_access : VkAccessFlags(VK_ACCESS_MEMORY_READ_BIT),
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.buffer = destination.handle,
				.offset = 0,
				.size = size,
			};
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     mesh ? VK_PIPELINE_STAGE_VERTEX_INPUT_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			                     0, 0, nullptr, 1, &visible, 0, nullptr);
		});
		std::cout << "Device-local buffer: bytes=" << size << ", usage=" << usage
		          << ", memoryFlags=" << destination.memory_properties << ", mapped=no\n";
	} catch (...) {
		destroyBuffer(staging);
		destroyBuffer(destination);
		throw;
	}
	destroyBuffer(staging);
	return destination;
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.handle != VK_NULL_HANDLE) {
		vmaDestroyBuffer(internal::context.allocator, buffer.handle, buffer.allocation);
	}
	buffer = {};
}

}
