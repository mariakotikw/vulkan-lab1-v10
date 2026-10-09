#include "ui_observer.hpp"

#include <imgui.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ui_observer {
namespace {

struct Observation {
	std::map<std::string, std::array<float, 4>> rectangles;
	std::chrono::steady_clock::time_point last_write{};
	unsigned serial = 0;
	application::SceneState scene;
	std::array<application::TransformUniform, application::object_count> uniforms;
	double time = 0;
	std::vector<Draw> draws;
	std::map<uintptr_t, uintptr_t> descriptor_buffers;
};
Observation& observation() { static Observation value; return value; }
void vector(std::ostream& out, glm::vec3 v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
void matrix(std::ostream& out, const glm::mat4& m) {
	out << '[';
	for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
		if (c != 0 || r != 0) out << ',';
		out << m[c][r];
	}
	out << ']';
}

}

void beginFrame() {
	observation().rectangles.clear();
	observation().draws.clear();
}

void item(const char* name) {
	const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
	observation().rectangles[name] = { min.x, min.y, max.x, max.y };
}

void state(const application::SceneState& scene,
           const std::array<application::TransformUniform, application::object_count>& uniforms, double time) {
	auto& data = observation();
	data.scene = scene;
	data.uniforms = uniforms;
	data.time = time;
}

void draw(const Draw& value) { observation().draws.push_back(value); }
void descriptorWrite(uintptr_t descriptor_set, uintptr_t uniform_buffer) {
	observation().descriptor_buffers[descriptor_set] = uniform_buffer;
}

void endFrame() {
	const auto path = std::getenv("LAB1_UI_OBSERVATION_FILE");
	if (path == nullptr || path[0] == '\0') return;
	auto& data = observation();
	const auto& camera = data.scene.camera;
	const auto& state = data.scene.objects[data.scene.selected_object];
	const auto& uniform = data.uniforms[data.scene.selected_object];
	const auto now = std::chrono::steady_clock::now();
	if (now - data.last_write < std::chrono::milliseconds(50)) return;
	data.last_write = now;
	const std::string temporary = std::string(path) + ".tmp";
	std::ofstream out(temporary, std::ios::trunc);
	if (!out) return;
	out << std::setprecision(10);
#ifdef _WIN32
	out << "{\"pid\":" << GetCurrentProcessId();
#else
	out << "{\"pid\":" << getpid();
#endif
	out << ",\"serial\":" << ++data.serial << ",\"time\":" << data.time
	    << ",\"selectedObject\":" << data.scene.selected_object
	    << ",\"projection\":\"" << (camera.projection == application::Projection::Perspective ? "Perspective" : "Orthographic")
	    << "\",\"fov\":" << camera.fov_degrees << ",\"orthoHeight\":" << camera.ortho_half_height;
	out << ",\"position\":"; vector(out, state.transform.position);
	out << ",\"rotation\":"; vector(out, state.transform.rotation_degrees);
	out << ",\"scale\":"; vector(out, state.transform.scale);
	out << ",\"color\":"; vector(out, state.user_color);
	out << ",\"playing\":" << (state.animation.playing ? "true" : "false")
	    << ",\"phase\":" << state.animation.phase << ",\"speed\":" << state.animation.speed
	    << ",\"radius\":" << state.animation.radius << ",\"amplitude\":" << state.animation.vertical_amplitude;
	out << ",\"model\":"; matrix(out, state.modelMatrix());
	out << ",\"mvp\":"; matrix(out, uniform.mvp);
	out << ",\"objects\":[";
	for (size_t i = 0; i < data.scene.objects.size(); ++i) {
		if (i != 0) out << ',';
		out << "{\"mvp\":"; matrix(out, data.uniforms[i].mvp);
		out << ",\"color\":"; vector(out, glm::vec3(data.uniforms[i].user_color));
		out << ",\"position\":"; vector(out, data.scene.objects[i].transform.position);
		out << '}';
	}
	out << "],\"draws\":[";
	for (size_t i = 0; i < data.draws.size(); ++i) {
		if (i != 0) out << ',';
		const auto& draw = data.draws[i];
		out << std::hex << "{\"descriptorSet\":\"" << draw.descriptor_set
		    << "\",\"uniformBuffer\":\"" << draw.uniform_buffer
		    << "\",\"descriptorUniformBuffer\":\"" << data.descriptor_buffers.at(draw.descriptor_set)
		    << "\",\"pipeline\":\"" << draw.pipeline
		    << "\",\"vertexBuffer\":\"" << draw.vertex_buffer
		    << "\",\"indexBuffer\":\"" << draw.index_buffer
		    << "\",\"indexCount\":" << std::dec << draw.index_count << ",\"uploadedMvp\":";
		matrix(out, draw.uploaded_uniform.mvp);
		out << ",\"uploadedColor\":"; vector(out, glm::vec3(draw.uploaded_uniform.user_color));
		out << '}';
	}
	out << ']';
	const auto display = ImGui::GetIO().DisplaySize;
	out << ",\"display\":[" << display.x << ',' << display.y << "],\"rects\":{";
	bool first = true;
	for (const auto& [name, rect] : data.rectangles) {
		if (!first) out << ',';
		first = false;
		out << '"' << name << "\":[" << rect[0] << ',' << rect[1] << ',' << rect[2] << ',' << rect[3] << ']';
	}
	out << "}}\n";
	out.close();
#ifdef _WIN32
	MoveFileExA(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING);
#else
	std::rename(temporary.c_str(), path);
#endif
}

}
