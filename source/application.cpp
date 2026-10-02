#include "application.hpp"
#include "dodecahedron.hpp"
#include "graphics.hpp"

#include <imgui.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace application {

namespace {

struct alignas(16) TransformUniform {
	glm::mat4 mvp;
};
static_assert(sizeof(TransformUniform) == 64);
static_assert(alignof(TransformUniform) >= 16);
static_assert(std::is_standard_layout_v<geometry::Vertex>);

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer uniform_buffer;
VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
uint32_t index_count = 0;

void check(VkResult result, const char* operation) {
	if (result != VK_SUCCESS) {
		throw std::runtime_error(std::string(operation) + " failed, VkResult=" + std::to_string(result));
	}
}

// Temporary modules are destroyed even if pipeline creation fails.
struct ShaderModule {
	VkShaderModule handle = VK_NULL_HANDLE;
	explicit ShaderModule(const char* path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			throw std::runtime_error(std::string("Cannot open shader: ") + path);
		}
		const auto size = file.tellg();
		if (size <= 0 || size % sizeof(uint32_t) != 0) {
			throw std::runtime_error(std::string("Invalid SPIR-V size: ") + path);
		}
		std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
			throw std::runtime_error(std::string("Cannot read shader: ") + path);
		}
		const VkShaderModuleCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = static_cast<size_t>(size),
			.pCode = code.data(),
		};
		check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &handle),
		      "vkCreateShaderModule");
	}
	ShaderModule(const ShaderModule&) = delete;
	ShaderModule& operator=(const ShaderModule&) = delete;
	~ShaderModule() {
		vkDestroyShaderModule(graphics::internal::context.device, handle, nullptr);
	}
};

void createDescriptors() {
	const auto device = graphics::internal::context.device;
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};
	check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout),
	      "vkCreateDescriptorSetLayout");
	const VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool), "vkCreateDescriptorPool");
	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};
	check(vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set), "vkAllocateDescriptorSets");
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = uniform_buffer.handle,
		.offset = 0,
		.range = sizeof(TransformUniform),
	};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void createPipeline() {
	const auto& context = graphics::internal::context;
	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};
	check(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout),
	      "vkCreatePipelineLayout");
	const ShaderModule vertex_shader("shaders/dodecahedron.vert.spv");
	const ShaderModule fragment_shader("shaders/dodecahedron.frag.spv");
	const VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader.handle, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader.handle, .pName = "main" },
	};
	const VkVertexInputBindingDescription binding = {
		.binding = 0, .stride = sizeof(geometry::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attributes[] = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(geometry::Vertex, position) },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(geometry::Vertex, color) },
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState color_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &color_attachment,
	};
	const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, .pDynamicStates = dynamic_states,
	};
	const VkGraphicsPipelineCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2, .pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamic,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};
	check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
	      "vkCreateGraphicsPipelines");
}

} // namespace

bool initialize() {
	try {
		const auto mesh = geometry::makeDodecahedron();
		vertex_buffer = graphics::createMappedBuffer(sizeof(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), sizeof(mesh.vertices));
		index_buffer = graphics::createMappedBuffer(sizeof(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		graphics::writeBuffer(index_buffer, mesh.indices.data(), sizeof(mesh.indices));
		index_count = static_cast<uint32_t>(mesh.indices.size());
		uniform_buffer = graphics::createMappedBuffer(sizeof(TransformUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		createDescriptors();
		createPipeline();
		std::cout << "Dodecahedron: 20 vertices, 12 pentagons, 36 triangles, 108 indices\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "Application initialization: " << error.what() << '\n';
		shutdown();
		return false;
	}
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
	pipeline = VK_NULL_HANDLE;
	pipeline_layout = VK_NULL_HANDLE;
	descriptor_pool = VK_NULL_HANDLE;
	descriptor_set_layout = VK_NULL_HANDLE;
	descriptor_set = VK_NULL_HANDLE;
	graphics::destroyBuffer(uniform_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update([[maybe_unused]] double time) {
	const auto extent = graphics::internal::context.swapchain_extent;
	const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	glm::mat4 model = glm::rotate(glm::mat4(1), glm::radians(5.0f), glm::vec3(1, 0, 0));
	model = glm::rotate(model, glm::radians(10.0f), glm::vec3(0, 1, 0));
	const glm::mat4 view = glm::lookAtRH(glm::vec3(3.5f), glm::vec3(0), glm::vec3(0, 1, 0));
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
	// Vulkan uses depth [0,1]; its positive viewport height points Y downward.
	projection[1][1] *= -1.0f;
	const TransformUniform uniform = { projection * view * model };
	// main calls prepare() first, so the previous GPU read has completed.
	graphics::writeBuffer(uniform_buffer, &uniform, sizeof(uniform));

	// Demo supplies its own SetNextWindow defaults; keep it beside the model on resize.
	ImGui::SetWindowPos("Dear ImGui Demo", ImVec2(ImGui::GetIO().DisplaySize.x - 340, 20));
	ImGui::SetWindowSize("Dear ImGui Demo", ImVec2(320, 450));
	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
	// prepare() has waited for the previous submission before this buffer is reused.
	if (vkResetCommandBuffer(fd.command_buffer, 0) != VK_SUCCESS) {
		throw std::runtime_error("Failed to reset application command buffer");
	}

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if (vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin) != VK_SUCCESS) {
		throw std::runtime_error("Failed to begin application command buffer");
	}

	auto& context = graphics::internal::context;
	const VkClearValue clear_values[] = {
		{ .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
		{ .depthStencil = { .depth = 1.0f, .stencil = 0 } },
	};
	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = 2,
		.pClearValues = clear_values,
	};
	// The existing pass clears the attachments and prepares the color image for ImGui.
	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	const VkViewport viewport = {
		.x = 0, .y = 0,
		.width = static_cast<float>(context.swapchain_extent.width),
		.height = static_cast<float>(context.swapchain_extent.height),
		.minDepth = 0, .maxDepth = 1,
	};
	const VkRect2D scissor = { .offset = { 0, 0 }, .extent = context.swapchain_extent };
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
	vkCmdEndRenderPass(fd.command_buffer);

	if (vkEndCommandBuffer(fd.command_buffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to end application command buffer");
	}
}

} // namespace application
