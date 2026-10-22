cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

add_executable(${PROJECT_NAME}
	source/main.cpp source/graphics_internal.cpp source/application.cpp
	source/graphics.cpp source/dodecahedron.cpp source/application_state.cpp
	source/lighting.cpp source/light_gizmos.cpp source/application_ui.cpp)

if(LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
		target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.3
	GIT_SHALLOW TRUE
)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Use header-only GLM" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "Disable dependency tests" FORCE)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
	glm
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
	glm::glm
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

# Compile shaders
find_program(GLSLC_FOUND glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
if(GLSLC_FOUND)
	set(_SHADER_BINARIES)

	macro(compile_shader SHADER_FILE)
		set(SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE})
		set(SHADER_BINARY ${SHADER_FILE}.spv)
		set(SHADER_BINARY_PATH ${CMAKE_SOURCE_DIR}/shaders/${SHADER_BINARY})

		add_custom_command(
			OUTPUT ${SHADER_BINARY_PATH}
			COMMAND ${GLSLC_FOUND} ${SHADER_SOURCE} -o ${SHADER_BINARY_PATH}
			DEPENDS ${SHADER_SOURCE} ${CMAKE_SOURCE_DIR}/shaders/scene.glsl ${CMAKE_SOURCE_DIR}/shaders/object.glsl
			COMMENT "Compiling ${SHADER_FILE} shader"
			VERBATIM
		)

		list(APPEND _SHADER_BINARIES ${SHADER_BINARY_PATH})
	endmacro()

	# To compile shader file, use compile_shader function with a file name
	# of a shader inside shaders directory. See example below:

	# compile_shader(example.vert)
	# compile_shader(example.frag)

	compile_shader(dodecahedron.vert)
	compile_shader(dodecahedron.frag)
	compile_shader(gizmo.vert)
	compile_shader(gizmo.frag)

	add_custom_target(shaders DEPENDS ${_SHADER_BINARIES})
	add_dependencies(${PROJECT_NAME} shaders)
endif()

option(LAB1_ENABLE_UI_TEST_HOOKS "Observe ImGui rectangles and uniforms for desktop tests" OFF)
if(LAB1_ENABLE_UI_TEST_HOOKS)
	target_compile_definitions(${PROJECT_NAME} PRIVATE LAB1_ENABLE_UI_TEST_HOOKS)
	target_include_directories(${PROJECT_NAME} PRIVATE tests source)
	target_sources(${PROJECT_NAME} PRIVATE tests/ui_observer.cpp)
endif()

include(CTest)
if(BUILD_TESTING)
	add_executable(dodecahedron-tests tests/dodecahedron_tests.cpp source/dodecahedron.cpp)
	target_include_directories(dodecahedron-tests PRIVATE source)
	target_link_libraries(dodecahedron-tests PRIVATE glm::glm)
	target_compile_features(dodecahedron-tests PRIVATE cxx_std_20)
	add_test(NAME dodecahedron-geometry COMMAND dodecahedron-tests)
	add_executable(application-state-tests tests/application_state_tests.cpp source/application_state.cpp source/lighting.cpp)
	target_include_directories(application-state-tests PRIVATE source)
	target_link_libraries(application-state-tests PRIVATE glm::glm)
	target_compile_features(application-state-tests PRIVATE cxx_std_20)
	add_test(NAME application-controls COMMAND application-state-tests)
	add_executable(lighting-tests tests/lighting_tests.cpp source/application_state.cpp source/lighting.cpp source/light_gizmos.cpp source/dodecahedron.cpp)
	target_include_directories(lighting-tests PRIVATE source)
	target_link_libraries(lighting-tests PRIVATE glm::glm)
	target_compile_features(lighting-tests PRIVATE cxx_std_20)
	add_test(NAME camera-lighting-gizmos COMMAND lighting-tests)
	find_package(Python3 QUIET COMPONENTS Interpreter)
	find_program(SPIRV_DIS spirv-dis HINTS "$ENV{VULKAN_SDK}/Bin")
	if(Python3_FOUND AND SPIRV_DIS)
		add_test(NAME shader-memory-layout COMMAND ${Python3_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/tests/verify_shader_layout.py
			--spirv-dis ${SPIRV_DIS} --shader-dir ${CMAKE_SOURCE_DIR}/shaders)
	endif()
endif()
