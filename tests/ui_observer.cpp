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

} // namespace

void beginFrame() { observation().rectangles.clear(); }

void item(const char* name) {
	const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
	observation().rectangles[name] = { min.x, min.y, max.x, max.y };
}

void frame(const application::ControlsState& state, const application::TransformUniform& uniform, double time) {
	const auto path = std::getenv("LAB1_UI_OBSERVATION_FILE");
	if (path == nullptr || path[0] == '\0') return;
	auto& data = observation();
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
	out << ",\"serial\":" << ++data.serial << ",\"time\":" << time
	    << ",\"projection\":\"" << (state.projection == application::Projection::Perspective ? "Perspective" : "Orthographic")
	    << "\",\"fov\":" << state.fov_degrees << ",\"orthoHeight\":" << state.ortho_half_height;
	out << ",\"position\":"; vector(out, state.transform.position);
	out << ",\"rotation\":"; vector(out, state.transform.rotation_degrees);
	out << ",\"scale\":"; vector(out, state.transform.scale);
	out << ",\"color\":"; vector(out, state.user_color);
	out << ",\"playing\":" << (state.animation.playing ? "true" : "false")
	    << ",\"phase\":" << state.animation.phase << ",\"speed\":" << state.animation.speed
	    << ",\"radius\":" << state.animation.radius << ",\"amplitude\":" << state.animation.vertical_amplitude;
	out << ",\"model\":"; matrix(out, state.modelMatrix());
	out << ",\"mvp\":"; matrix(out, uniform.mvp);
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

} // namespace ui_observer
