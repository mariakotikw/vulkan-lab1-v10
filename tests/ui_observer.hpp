#pragma once

#include "application_state.hpp"

namespace ui_observer {
struct Draw {
	uintptr_t descriptor_set, uniform_buffer, pipeline, vertex_buffer, index_buffer;
	uint32_t index_count;
	application::ObjectUniform uploaded_uniform;
};
void beginFrame();
void item(const char* name, float width = 0);
void state(const application::SceneState& scene,
           const std::array<application::ObjectUniform, application::object_count>& uniforms, double time);
void draw(const Draw& value);
void descriptorWrite(uintptr_t descriptor_set, uintptr_t uniform_buffer);
void lighting(const application::SceneUniform& uploaded_scene, uintptr_t scene_set,
              uintptr_t point_buffer, uintptr_t spot_buffer);
void endFrame();
}
