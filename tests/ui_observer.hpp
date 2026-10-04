#pragma once

#include "application_state.hpp"

// Only compiled when LAB1_ENABLE_UI_TEST_HOOKS is explicitly enabled.
// Observes real UI interaction; never changes application state or GPU commands.
namespace ui_observer {
void beginFrame();
void item(const char* name);
void frame(const application::ControlsState& state, const application::TransformUniform& uniform, double time);
}
