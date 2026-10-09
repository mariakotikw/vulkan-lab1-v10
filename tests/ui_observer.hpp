#pragma once

#include "application_state.hpp"

namespace ui_observer {
struct Draw {
	uintptr_t descriptor_set, uniform_buffer, pipeline, vertex_buffer, index_buffer;
	uint32_t index_count;
	application::TransformUniform uploaded_uniform;
};
void beginFrame();
void item(const char* name);
void state(const application::SceneState& scene,
           const std::array<application::TransformUniform, application::object_count>& uniforms, double time);
void draw(const Draw& value);
void descriptorWrite(uintptr_t descriptor_set, uintptr_t uniform_buffer);
void endFrame();
}
