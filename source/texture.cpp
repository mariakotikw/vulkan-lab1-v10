#include "texture.hpp"

#include "graphics.hpp"
#include "image_data.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace graphics {
namespace {

Texture uploadRgba(const void* pixels, uint32_t width, uint32_t height, bool srgb) {
	if (pixels == nullptr || width == 0 || height == 0) throw std::runtime_error("Cannot upload empty texture");
	Texture texture{};
	texture.width = width;
	texture.height = height;
	texture.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
	VkPhysicalDeviceProperties device_properties{};
	vkGetPhysicalDeviceProperties(internal::context.physical_device, &device_properties);
	if (width > device_properties.limits.maxImageDimension2D || height > device_properties.limits.maxImageDimension2D) {
		throw std::runtime_error("Texture exceeds the GPU's maxImageDimension2D");
	}
	VkFormatProperties format_properties{};
	vkGetPhysicalDeviceFormatProperties(internal::context.physical_device, texture.format, &format_properties);
	constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
	                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
	                                        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if ((format_properties.optimalTilingFeatures & needed) != needed) {
		throw std::runtime_error("GPU does not support sampled, linearly filtered RGBA8 textures");
	}
	Buffer staging{};
	try {
		const VkDeviceSize byte_count = VkDeviceSize(width) * height * 4;
		staging = createMappedBuffer(byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		writeBuffer(staging, pixels, byte_count);
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = texture.format,
			.extent = { width, height, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		const VmaAllocationCreateInfo allocation_info = {
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};
		if (vmaCreateImage(internal::context.allocator, &image_info, &allocation_info,
		                   &texture.image, &texture.allocation, nullptr) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate device-local texture image");
		}
		vmaGetAllocationMemoryProperties(internal::context.allocator, texture.allocation,
		                                 &texture.memory_properties);
		if ((texture.memory_properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) {
			throw std::runtime_error("Texture allocation does not use device-local memory");
		}
		immediateSubmit([&](VkCommandBuffer command) {
			const VkImageSubresourceRange range = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0, .levelCount = 1,
				.baseArrayLayer = 0, .layerCount = 1,
			};
			const VkImageMemoryBarrier to_transfer = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = texture.image,
				.subresourceRange = range,
			};
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &to_transfer);
			const VkBufferImageCopy region = {
				.bufferOffset = 0,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
				.imageOffset = { 0, 0, 0 },
				.imageExtent = { width, height, 1 },
			};
			vkCmdCopyBufferToImage(command, staging.handle, texture.image,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			const VkImageMemoryBarrier to_sample = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = texture.image,
				.subresourceRange = range,
			};
			vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &to_sample);
		});
		const VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = texture.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = texture.format,
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		};
		if (vkCreateImageView(internal::context.device, &view_info, nullptr, &texture.view) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create texture image view");
		}
		const VkSamplerCreateInfo sampler_info = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 1.0f,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		if (vkCreateSampler(internal::context.device, &sampler_info, nullptr, &texture.sampler) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create texture sampler");
		}
	} catch (...) {
		destroyBuffer(staging);
		destroyTexture(texture);
		throw;
	}
	destroyBuffer(staging);
	return texture;
}

}

Texture loadTexture(const char* path, bool srgb) {
	const ImageData image = loadImageRgba(path);
	Texture texture = uploadRgba(image.pixels.data(), image.width, image.height, srgb);
	std::cout << "Texture: " << path << ", " << texture.width << 'x' << texture.height
	          << ", " << (srgb ? "sRGB" : "UNORM") << ", memoryFlags=" << texture.memory_properties << '\n';
	return texture;
}

Texture createBlackTexture() {
	constexpr std::array<uint8_t, 4> black = { 0, 0, 0, 255 };
	return uploadRgba(black.data(), 1, 1, false);
}

void destroyTexture(Texture& texture) {
	const auto device = internal::context.device;
	if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture.sampler, nullptr);
	if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device, texture.view, nullptr);
	if (texture.image != VK_NULL_HANDLE) vmaDestroyImage(internal::context.allocator, texture.image, texture.allocation);
	texture = {};
}

}
