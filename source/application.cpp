#include "application.hpp"
#include "application_state.hpp"
#include "dodecahedron.hpp"
#include "graphics.hpp"
#include "application_ui.hpp"
#include "light_gizmos.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifdef LAB1_ENABLE_UI_TEST_HOOKS
#include "ui_observer.hpp"
#define LAB1_OBSERVE_ITEM(name) ui_observer::item(name)
#else
#define LAB1_OBSERVE_ITEM(name) ((void)0)
#endif

namespace application {

namespace {

static_assert(std::is_standard_layout_v<geometry::Vertex>);

SceneState scene;

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
std::array<graphics::Buffer, object_count> object_uniform_buffers;
graphics::Buffer scene_uniform_buffer;
graphics::Buffer point_light_buffer, spot_light_buffer;
graphics::Buffer gizmo_triangle_buffer, gizmo_line_buffer;
VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
VkDescriptorSet scene_descriptor_set = VK_NULL_HANDLE;
VkPipeline gizmo_triangle_pipeline = VK_NULL_HANDLE;
VkPipeline gizmo_line_pipeline = VK_NULL_HANDLE;
uint32_t gizmo_triangle_count = 0, gizmo_line_count = 0;
VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
std::array<VkDescriptorSet, object_count> descriptor_sets{};
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
uint32_t index_count = 0;

VkExtent2D sceneExtent() {
	const auto extent = graphics::internal::context.swapchain_extent;
	const float pixels_per_ui_unit = extent.width / std::max(ImGui::GetIO().DisplaySize.x, 1.0f);
	const auto width = static_cast<uint32_t>(std::max(1.0f, extent.width - 384.0f * pixels_per_ui_unit));
	return { width, extent.height };
}

void check(VkResult result, const char* operation) {
	if (result != VK_SUCCESS) {
		throw std::runtime_error(std::string(operation) + " failed, VkResult=" + std::to_string(result));
	}
}

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

// Called only after prepare() has waited for the one in-flight frame's fence.
// Allocate first so an allocation failure leaves the existing buffer usable.
bool ensureCapacity(graphics::Buffer& buffer, VkDeviceSize required, VkBufferUsageFlags usage) {
	if (buffer.size >= required) return false;
	auto capacity = std::max(required, buffer.size * 2);
	if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
		capacity = std::min(capacity, static_cast<VkDeviceSize>(properties.limits.maxStorageBufferRange));
		if (capacity < required) throw std::runtime_error("Light array exceeds maxStorageBufferRange");
	}
	auto replacement = graphics::createMappedBuffer(capacity, usage);
	graphics::destroyBuffer(buffer);
	buffer = replacement;
	return true;
}

void updateSceneDescriptors() {
	const VkDescriptorBufferInfo infos[] = {
		{ scene_uniform_buffer.handle, 0, sizeof(SceneUniform) },
		{ point_light_buffer.handle, 0, point_light_buffer.size },
		{ spot_light_buffer.handle, 0, spot_light_buffer.size }
	};
	VkWriteDescriptorSet writes[3]{};
	for (uint32_t i = 0; i < 3; ++i) {
		writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_descriptor_set, .dstBinding = i, .descriptorCount = 1,
			.descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &infos[i] };
	}
	vkUpdateDescriptorSets(graphics::internal::context.device, 3, writes, 0, nullptr);
}

void createDescriptors() {
	const auto device = graphics::internal::context.device;
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
	};
	const VkDescriptorSetLayoutCreateInfo scene_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3, .pBindings = scene_bindings
	};
	check(vkCreateDescriptorSetLayout(device, &scene_info, nullptr, &scene_set_layout), "scene layout");
	const VkDescriptorSetLayoutBinding binding = {
		0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
	};
	const VkDescriptorSetLayoutCreateInfo object_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1, .pBindings = &binding
	};
	check(vkCreateDescriptorSetLayout(device, &object_info, nullptr, &descriptor_set_layout), "object layout");
	const VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, object_count + 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 }
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = object_count + 1, .poolSizeCount = 2, .pPoolSizes = pool_sizes
	};
	check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool), "descriptor pool");
	const VkDescriptorSetAllocateInfo scene_allocate = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptor_pool,
		.descriptorSetCount = 1, .pSetLayouts = &scene_set_layout
	};
	check(vkAllocateDescriptorSets(device, &scene_allocate, &scene_descriptor_set), "scene descriptor");
	std::array<VkDescriptorSetLayout, object_count> layouts;
	layouts.fill(descriptor_set_layout);
	const VkDescriptorSetAllocateInfo allocate = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptor_pool,
		.descriptorSetCount = object_count, .pSetLayouts = layouts.data()
	};
	check(vkAllocateDescriptorSets(device, &allocate, descriptor_sets.data()), "object descriptors");
	updateSceneDescriptors();
	for (size_t i = 0; i < object_count; ++i) {
		const VkDescriptorBufferInfo info = { object_uniform_buffers[i].handle, 0, sizeof(ObjectUniform) };
		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor_sets[i], .dstBinding = 0,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &info
		};
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
#ifdef LAB1_ENABLE_UI_TEST_HOOKS
		ui_observer::descriptorWrite(reinterpret_cast<uintptr_t>(write.dstSet), reinterpret_cast<uintptr_t>(info.buffer));
#endif
	}
}

void createPipeline(bool gizmo = false, bool lines = false) {
	const auto& context = graphics::internal::context;
	const VkDescriptorSetLayout layouts[] = { scene_set_layout, descriptor_set_layout };
	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = layouts,
	};
	if (!gizmo) check(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout),
	      "vkCreatePipelineLayout");
	const ShaderModule vertex_shader(gizmo ? "shaders/gizmo.vert.spv" : "shaders/dodecahedron.vert.spv");
	const ShaderModule fragment_shader(gizmo ? "shaders/gizmo.frag.spv" : "shaders/dodecahedron.frag.spv");
	const VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader.handle, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader.handle, .pName = "main" },
	};
	const VkVertexInputBindingDescription binding = {
		.binding = 0, .stride = static_cast<uint32_t>(gizmo ? sizeof(GizmoVertex) : sizeof(geometry::Vertex)), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attributes[] = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(geometry::Vertex, position) },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = static_cast<uint32_t>(gizmo ? offsetof(GizmoVertex, color) : offsetof(geometry::Vertex, normal)) },
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = gizmo ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
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
	check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, gizmo ? (lines ? &gizmo_line_pipeline : &gizmo_triangle_pipeline) : &pipeline),
	      "vkCreateGraphicsPipelines");
}


}

bool initialize() {
	try {
		const auto mesh = geometry::makeFlatDodecahedron();
		vertex_buffer = graphics::createMappedBuffer(sizeof(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), sizeof(mesh.vertices));
		index_buffer = graphics::createMappedBuffer(sizeof(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		graphics::writeBuffer(index_buffer, mesh.indices.data(), sizeof(mesh.indices));
		index_count = static_cast<uint32_t>(mesh.indices.size());
		for (auto& buffer : object_uniform_buffers) {
			buffer = graphics::createMappedBuffer(sizeof(ObjectUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		}
		scene_uniform_buffer = graphics::createMappedBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		point_light_buffer = graphics::createMappedBuffer(sizeof(GpuPointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		spot_light_buffer = graphics::createMappedBuffer(sizeof(GpuSpotLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createDescriptors();
		createPipeline();
		createPipeline(true, false);
		createPipeline(true, true);
		std::cout << "Lab 2: 20 geometric / 60 flat vertices, 12 pentagons, 36 triangles; "
		          << "Scene set 0 (UBO + point/spot SSBO), Object set 1 (2 UBOs)\n";
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
	vkDestroyPipeline(context.device, gizmo_triangle_pipeline, nullptr);
	vkDestroyPipeline(context.device, gizmo_line_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	pipeline = gizmo_triangle_pipeline = gizmo_line_pipeline = VK_NULL_HANDLE;
	scene_set_layout = VK_NULL_HANDLE;
	scene_descriptor_set = VK_NULL_HANDLE;
	pipeline_layout = VK_NULL_HANDLE;
	descriptor_pool = VK_NULL_HANDLE;
	descriptor_set_layout = VK_NULL_HANDLE;
	descriptor_sets.fill(VK_NULL_HANDLE);
	for (auto& buffer : object_uniform_buffers) graphics::destroyBuffer(buffer);
	graphics::destroyBuffer(scene_uniform_buffer);
	graphics::destroyBuffer(point_light_buffer);
	graphics::destroyBuffer(spot_light_buffer);
	graphics::destroyBuffer(gizmo_triangle_buffer);
	graphics::destroyBuffer(gizmo_line_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void smokeStep(uint32_t frame) {
	if (frame % 60 != 0) return;
	const uint32_t phase = frame / 60;
	std::cout << "SMOKE phase " << phase << '\n';
	if (phase == 1 || phase == 3) {
		scene.point_lights.clear(); scene.spot_lights.clear();
	} else if (phase == 2) {
		for (int i = 0; i < 33; ++i) {
			scene.addPointLight(); scene.addSpotLight();
			scene.point_lights.back().position.x = (i - 16) * 0.15f;
			scene.point_lights.back().intensity = 0.05f;
			scene.spot_lights.back().intensity = 0.05f;
		}
	} else if (phase == 4) {
		scene.addPointLight(); scene.addSpotLight();
		scene.spot_lights[0].inner_degrees = scene.spot_lights[0].outer_degrees = 25;
		scene.spot_lights[0].direction = glm::vec3(0);
		scene.objects[0].transform.scale = { 0.3f, 0.8f, 1.1f };
		scene.objects[0].material.shininess = 128;
	} else if (phase == 5) {
		scene.spot_lights[0].inner_degrees = 70; scene.spot_lights[0].outer_degrees = 15;
		scene.spot_lights[0].direction = { 1, -1, -1 };
		scene.show_light_gizmos = false;
		scene.camera.position.x += 1; scene.camera.yaw_degrees -= 8;
	} else if (phase == 6) {
		scene.show_light_gizmos = true;
		scene.camera.projection = Projection::Orthographic;
	} else if (phase == 7) {
		scene = SceneState{};
		scene.objects[0].animation.playing = true;
	} else if (phase == 8) {
		scene.objects[0].animation.playing = false;
		scene.objects[0].animation.reset();
	}
}

void update(double time) {
#ifdef LAB1_ENABLE_UI_TEST_HOOKS
	ui_observer::beginFrame();
#endif
	scene.advance(time);
	drawControls(scene);
	scene.sanitize();
	const auto extent = sceneExtent();
	const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	const auto scene_uniform = scene.sceneUniform(aspect);
	graphics::writeBuffer(scene_uniform_buffer, &scene_uniform, sizeof(scene_uniform));
	std::vector<GpuPointLight> points;
	std::vector<GpuSpotLight> spots;
	for (const auto& light : scene.point_lights) points.push_back(light.gpu());
	for (const auto& light : scene.spot_lights) spots.push_back(light.gpu());
	// Non-empty descriptor ranges remain valid even when the logical light count is zero.
	if (points.empty()) points.emplace_back(GpuPointLight{});
	if (spots.empty()) spots.emplace_back(GpuSpotLight{});
	const VkDeviceSize point_bytes = points.size() * sizeof(GpuPointLight);
	const VkDeviceSize spot_bytes = spots.size() * sizeof(GpuSpotLight);
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
	if (point_bytes > properties.limits.maxStorageBufferRange || spot_bytes > properties.limits.maxStorageBufferRange)
		throw std::runtime_error("Light array exceeds maxStorageBufferRange");
	const bool point_changed = ensureCapacity(point_light_buffer, point_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	const bool spot_changed = ensureCapacity(spot_light_buffer, spot_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	if (point_changed || spot_changed) updateSceneDescriptors();
	graphics::writeBuffer(point_light_buffer, points.data(), point_bytes);
	graphics::writeBuffer(spot_light_buffer, spots.data(), spot_bytes);
	const auto gizmos = makeLightGizmos(scene);
	gizmo_triangle_count = static_cast<uint32_t>(gizmos.triangles.size());
	gizmo_line_count = static_cast<uint32_t>(gizmos.lines.size());
	if (gizmo_triangle_count) {
		const auto bytes = gizmos.triangles.size() * sizeof(GizmoVertex);
		ensureCapacity(gizmo_triangle_buffer, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		graphics::writeBuffer(gizmo_triangle_buffer, gizmos.triangles.data(), bytes);
	}
	if (gizmo_line_count) {
		const auto bytes = gizmos.lines.size() * sizeof(GizmoVertex);
		ensureCapacity(gizmo_line_buffer, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		graphics::writeBuffer(gizmo_line_buffer, gizmos.lines.data(), bytes);
	}
	const auto uniforms = scene.objectUniforms();
	for (size_t i = 0; i < object_uniform_buffers.size(); ++i) {
		graphics::writeBuffer(object_uniform_buffers[i], &uniforms[i], sizeof(ObjectUniform));
	}
#ifdef LAB1_ENABLE_UI_TEST_HOOKS
	ui_observer::state(scene, uniforms, time);
	ui_observer::lighting(*static_cast<const SceneUniform*>(scene_uniform_buffer.mapped),
	                     reinterpret_cast<uintptr_t>(scene_descriptor_set),
	                     reinterpret_cast<uintptr_t>(point_light_buffer.handle),
	                     reinterpret_cast<uintptr_t>(spot_light_buffer.handle));
#endif
}

void render(const graphics::internal::FrameData& fd) {
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
		{ .color = { .float32 = { 0.012f, 0.018f, 0.03f, 1.0f } } },
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
	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	const auto extent = sceneExtent();
	const VkViewport viewport = {
		.x = 0, .y = 0,
		.width = static_cast<float>(extent.width),
		.height = static_cast<float>(extent.height),
		.minDepth = 0, .maxDepth = 1,
	};
	const VkRect2D scissor = { .offset = { 0, 0 }, .extent = extent };
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
	                        0, 1, &scene_descriptor_set, 0, nullptr);
	for (size_t i = 0; i < descriptor_sets.size(); ++i) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        pipeline_layout, 1, 1, &descriptor_sets[i], 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
#ifdef LAB1_ENABLE_UI_TEST_HOOKS
		ui_observer::draw({ reinterpret_cast<uintptr_t>(descriptor_sets[i]),
		                    reinterpret_cast<uintptr_t>(object_uniform_buffers[i].handle),
		                    reinterpret_cast<uintptr_t>(pipeline), reinterpret_cast<uintptr_t>(vertex_buffer.handle),
		                    reinterpret_cast<uintptr_t>(index_buffer.handle), index_count,
		                    *static_cast<const ObjectUniform*>(object_uniform_buffers[i].mapped) });
#endif
	}
	if (gizmo_triangle_count) {
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gizmo_triangle_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &gizmo_triangle_buffer.handle, &offset);
		vkCmdDraw(fd.command_buffer, gizmo_triangle_count, 1, 0, 0);
	}
	if (gizmo_line_count) {
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gizmo_line_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &gizmo_line_buffer.handle, &offset);
		vkCmdDraw(fd.command_buffer, gizmo_line_count, 1, 0, 0);
	}
	vkCmdEndRenderPass(fd.command_buffer);

	if (vkEndCommandBuffer(fd.command_buffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to end application command buffer");
	}
#ifdef LAB1_ENABLE_UI_TEST_HOOKS
	ui_observer::endFrame();
#endif
}

} // namespace application
