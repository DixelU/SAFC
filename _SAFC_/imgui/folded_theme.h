#pragma once

#include <imgui.h>

namespace safc::imgui_ui
{
// Call before NewFrame; repeated calls do not accumulate size scaling.
void apply_theme(float scale = 1.f);

// Pair every call with end_folded_window(), including when false is returned.
// Uses rectangular ImGui window input bounds, with a custom draggable caption.
bool begin_folded_window(const char* title, bool* open = nullptr,
    ImGuiWindowFlags flags = 0);
void end_folded_window();

// Parent geometry while a folded window is open; ordinary ImGui geometry
// queries inside begin/end refer to its scrolling content child.
ImVec2 folded_window_position();
ImVec2 folded_window_size();
}
