#include <cstdint>
#include <climits>

#include <iostream>
#include <string_view>
#include <exception>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

constexpr int32_t default_window_width = 1280;
constexpr int32_t default_window_height = 720;

constexpr char default_window_title[] = "Vulkan Lab 2 - Camera and Lighting - Variant 10";

GLFWwindow* glfw_window;

} // namespace

int main(int argc, char** argv) {
	int status = EXIT_SUCCESS;
	const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
	uint32_t frame = 0;

	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfw_window = glfwCreateWindow(default_window_width, default_window_height,
	                               default_window_title, nullptr, nullptr);
	if (glfw_window == nullptr) {
		status = EXIT_FAILURE;
		goto err_null_window;
	}

	glfwSetFramebufferSizeCallback(glfw_window, [](GLFWwindow*, int width, int height){
		if (width == 0 || height == 0) {
			return;
		}

		graphics::internal::resize(width, height);
	});

	if (ImGui::CreateContext() == nullptr) {
		std::cerr << "Failed to create ImGUI context\n";
		status = EXIT_FAILURE;
		goto err_imgui_init;
	}

	if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
		std::cerr << "Failed to initialize ImGUI GLFW backend for Vulkan renderer\n";
		status = EXIT_FAILURE;
		goto err_imgui_glfw_init;
	}

	if (!graphics::internal::initialize(glfw_window)) {
		std::cerr << "Failed to initialize graphics\n";
		status = EXIT_FAILURE;
		goto err_graphics_init;
	}

	if (!application::initialize()) {
		std::cerr << "Failed to initialize application\n";
		status = EXIT_FAILURE;
		goto err_application_init;
	}

	try {
	while (!glfwWindowShouldClose(glfw_window)) {
		const double time = glfwGetTime();

		glfwPollEvents();
		int framebuffer_width, framebuffer_height;
		glfwGetFramebufferSize(glfw_window, &framebuffer_width, &framebuffer_height);
		if (framebuffer_width == 0 || framebuffer_height == 0) {
			glfwWaitEvents();
			continue;
		}
		graphics::internal::FrameData fd = graphics::internal::prepare();
		if (fd.command_buffer == VK_NULL_HANDLE) {
			status = glfwWindowShouldClose(glfw_window) ? EXIT_SUCCESS : EXIT_FAILURE;
			break;
		}
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if (smoke) application::smokeStep(frame);
		application::update(time);
		ImGui::Render();

		application::render(fd);
		graphics::internal::submitAndPresent();
		if (smoke && ++frame == 600) glfwSetWindowShouldClose(glfw_window, GLFW_TRUE);
	}
	} catch (const std::exception& error) {
		std::cerr << "Rendering: " << error.what() << '\n';
		status = EXIT_FAILURE;
	}

	application::shutdown();
err_application_init:
	graphics::internal::shutdown();
	if (smoke && (!graphics::internal::context.validation_enabled ||
	              graphics::internal::context.validation_errors || graphics::internal::context.validation_warnings)) {
		status = EXIT_FAILURE;
	}
err_graphics_init:
	ImGui_ImplGlfw_Shutdown();
err_imgui_glfw_init:
	ImGui::DestroyContext();
err_imgui_init:
	glfwDestroyWindow(glfw_window);
err_null_window:
	glfwTerminate();

	return status;
}
