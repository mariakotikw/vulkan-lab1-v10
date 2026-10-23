#include "application_ui.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <string>

#ifdef LAB1_ENABLE_UI_TEST_HOOKS
#include "ui_observer.hpp"
#define OBSERVE(name) ui_observer::item(name)
#define OBSERVE_VALUE(name) ui_observer::item(name, ImGui::CalcItemWidth())
#else
#define OBSERVE(name) ((void)0)
#define OBSERVE_VALUE(name) ((void)0)
#endif

namespace application {
namespace {
bool section(const char* label, const char* id, ImGuiTreeNodeFlags flags = 0) {
	const bool open = ImGui::CollapsingHeader(label, flags);
	OBSERVE(id);
	(void)id;
	return open;
}
void pointControls(PointLight& light, const std::string& prefix) {
	(void)prefix;
	ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.03f);
	OBSERVE_VALUE((prefix + "_position").c_str());
	ImGui::ColorEdit3("Color", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);
	OBSERVE_VALUE((prefix + "_color").c_str());
	ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	OBSERVE_VALUE((prefix + "_intensity").c_str());
}
}
void drawControls(SceneState& scene) {
	const auto display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, display.x - 376.0f), 8));
	ImGui::SetNextWindowSize(ImVec2(std::min(368.0f, display.x), std::max(50.0f, display.y - 16)));
	const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("Lab 2 - Camera and Lighting", nullptr, flags)) {
		ImGui::TextUnformatted("Variant 10 / regular dodecahedron");
		if (section("Camera", "camera_header", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& camera = scene.camera;
			ImGui::DragFloat3("Camera XYZ", glm::value_ptr(camera.position), 0.04f);
			OBSERVE_VALUE("camera_position");
			ImGui::DragFloat("Yaw", &camera.yaw_degrees, 0.5f);
			OBSERVE_VALUE("yaw");
			ImGui::DragFloat("Pitch", &camera.pitch_degrees, 0.5f, -89, 89, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			OBSERVE_VALUE("pitch");
			if (ImGui::RadioButton("Perspective", camera.projection == Projection::Perspective)) camera.projection = Projection::Perspective;
			OBSERVE("perspective");
			ImGui::SameLine();
			if (ImGui::RadioButton("Orthographic", camera.projection == Projection::Orthographic)) camera.projection = Projection::Orthographic;
			OBSERVE("orthographic");
			if (camera.projection == Projection::Perspective) {
				ImGui::SliderFloat("FOV", &camera.fov_degrees, 15, 90, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
				OBSERVE_VALUE("fov");
			} else {
				ImGui::DragFloat("Half height", &camera.ortho_half_height, 0.02f, 1, 8, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				OBSERVE_VALUE("ortho_height");
			}
			ImGui::DragFloat("Near plane", &camera.near_plane, 0.01f, 0.01f, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::DragFloat("Far plane", &camera.far_plane, 0.5f, 0.11f, 10000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			if (ImGui::Button("Reset Camera")) camera.reset();
			OBSERVE("reset_camera");
		}
		if (section("Scene / Objects", "scene_header", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::RadioButton("Object 1", &scene.selected_object, 0);
			OBSERVE("object_1");
			ImGui::SameLine();
			ImGui::RadioButton("Object 2", &scene.selected_object, 1);
			OBSERVE("object_2");
			auto& object = scene.objects[scene.selected_object];
			ImGui::DragFloat3("Object XYZ", glm::value_ptr(object.transform.position), 0.02f);
			OBSERVE_VALUE("position");
			ImGui::DragFloat3("Rotation XYZ", glm::value_ptr(object.transform.rotation_degrees), 0.5f);
			OBSERVE_VALUE("rotation");
			ImGui::DragFloat3("Scale XYZ", glm::value_ptr(object.transform.scale), 0.01f, 0.05f, 5, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			OBSERVE_VALUE("scale");
			if (ImGui::Button("Reset Transform")) object.resetTransform();
			OBSERVE("reset_transform");
			const bool animation_open = ImGui::TreeNode("Animation");
			OBSERVE("animation_header");
			if (animation_open) {
				if (ImGui::Button(object.animation.playing ? "Pause" : "Play")) object.animation.playing = !object.animation.playing;
				OBSERVE("play_pause");
				ImGui::SameLine();
				if (ImGui::Button("Reset Animation")) object.animation.reset();
				OBSERVE("reset_animation");
				ImGui::DragFloat("Speed", &object.animation.speed, 0.02f, 0.1f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				OBSERVE_VALUE("speed");
				ImGui::DragFloat("Radius", &object.animation.radius, 0.01f, 0, 2, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				OBSERVE_VALUE("radius");
				ImGui::DragFloat("Vertical amplitude", &object.animation.vertical_amplitude, 0.01f, 0, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				OBSERVE_VALUE("amplitude");
				ImGui::TreePop();
			}
		}
		if (section("Material", "material_header", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& material = scene.objects[scene.selected_object].material;
			ImGui::Text("Object %d", scene.selected_object + 1);
			ImGui::ColorEdit3("Albedo", glm::value_ptr(material.albedo), ImGuiColorEditFlags_Float);
			OBSERVE_VALUE("color");
			ImGui::ColorEdit3("Specular Color", glm::value_ptr(material.specular), ImGuiColorEditFlags_Float);
			OBSERVE_VALUE("specular");
			ImGui::SliderFloat("Shininess", &material.shininess, 1, 256, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			OBSERVE_VALUE("shininess");
		}
		if (section("Ambient Light", "ambient_header")) {
			ImGui::ColorEdit3("Ambient RGB", glm::value_ptr(scene.ambient_color));
			ImGui::SliderFloat("Ambient intensity", &scene.ambient_intensity, 0, 2);
			OBSERVE_VALUE("ambient_intensity");
		}
		if (section("Directional Light", "directional_header")) {
			ImGui::TextWrapped("Direction is the travel direction of light rays.");
			ImGui::DragFloat3("Direction", glm::value_ptr(scene.directional.direction), 0.02f);
			ImGui::ColorEdit3("Directional RGB", glm::value_ptr(scene.directional.color));
			ImGui::SliderFloat("Directional intensity", &scene.directional.intensity, 0, 10);
			OBSERVE_VALUE("directional_intensity");
		}
		if (section("Point Lights", "point_header")) {
			if (ImGui::Button("Add Point Light")) scene.addPointLight();
			OBSERVE("add_point");
			ImGui::SameLine();
			if (ImGui::Button("Clear Points")) scene.point_lights.clear();
			OBSERVE("clear_points");
			for (size_t i = 0; i < scene.point_lights.size();) {
				ImGui::PushID(static_cast<int>(i));
				bool remove = false;
				const auto prefix = "point_" + std::to_string(i);
				const bool open = ImGui::TreeNode(("Point " + std::to_string(i + 1)).c_str());
				OBSERVE((prefix + "_header").c_str());
				if (open) {
					pointControls(scene.point_lights[i], prefix);
					remove = ImGui::Button("Remove Point");
					OBSERVE((prefix + "_remove").c_str());
					ImGui::TreePop();
				}
				ImGui::PopID();
				if (remove) scene.removePointLight(i); else ++i;
			}
		}
		if (section("Spot Lights", "spot_header")) {
			if (ImGui::Button("Add Spot Light")) scene.addSpotLight();
			OBSERVE("add_spot");
			ImGui::SameLine();
			if (ImGui::Button("Clear Spots")) scene.spot_lights.clear();
			OBSERVE("clear_spots");
			for (size_t i = 0; i < scene.spot_lights.size();) {
				ImGui::PushID(static_cast<int>(i));
				bool remove = false;
				const auto prefix = "spot_" + std::to_string(i);
				const bool open = ImGui::TreeNode(("Spot " + std::to_string(i + 1)).c_str());
				OBSERVE((prefix + "_header").c_str());
				if (open) {
					auto& light = scene.spot_lights[i];
					pointControls(light, prefix);
					ImGui::DragFloat3("Ray direction", glm::value_ptr(light.direction), 0.02f);
					OBSERVE_VALUE((prefix + "_direction").c_str());
					ImGui::SliderFloat("Inner angle", &light.inner_degrees, 0, 89, "%.1f deg");
					OBSERVE_VALUE((prefix + "_inner").c_str());
					ImGui::SliderFloat("Outer angle", &light.outer_degrees, 0, 89, "%.1f deg");
					OBSERVE_VALUE((prefix + "_outer").c_str());
					ImGui::TextWrapped("Half angles. Equal angles produce a hard edge; reversed angles are sorted.");
					remove = ImGui::Button("Remove Spot");
					OBSERVE((prefix + "_remove").c_str());
					ImGui::TreePop();
				}
				ImGui::PopID();
				if (remove) scene.removeSpotLight(i); else ++i;
			}
		}
		if (section("Light Visualization", "visualization_header")) {
			ImGui::Checkbox("Show Light Gizmos", &scene.show_light_gizmos);
			OBSERVE("gizmos");
			ImGui::SliderFloat("Cone guide length", &scene.gizmo_length, 0.1f, 10);
			ImGui::TextWrapped("Unlit cubes: light positions. White rings: inner/outer cones. Colored line: ray direction.");
		}
		ImGui::Text("%zu points / %zu spots | %.1f FPS", scene.point_lights.size(), scene.spot_lights.size(), ImGui::GetIO().Framerate);
	}
	ImGui::End();
}
}
