#pragma once

#include <imgui.h>

namespace safc::imgui_ui
{
inline constexpr float workspace_font_size = 17.f;

// Combined DPI and interface scale of the current font. Multiply fixed layout
// dimensions by this so they follow the text size.
inline float ui_scale()
{
	return ImGui::GetFontSize() / workspace_font_size;
}
inline float scaled(float pixels)
{
	return pixels * ui_scale();
}

// Call before NewFrame; repeated calls do not accumulate size scaling. Only the
// style is scaled: build the font atlas at workspace_font_size * scale.
void apply_theme(float scale = 1.f);

// Pair every call with end_folded_window(), including when false is returned.
// Uses rectangular ImGui window input bounds, with a custom draggable caption.
// Resizeable windows get a maximize button; double-clicking the caption toggles it too.
bool begin_folded_window(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0);
void end_folded_window();

// Area filled by maximized folded windows; the main viewport until it is set.
void set_maximized_window_area(ImVec2 position, ImVec2 size);
// Returns every maximized folded window to the placement it had before.
void restore_maximized_windows();

// Parent geometry while a folded window is open; ordinary ImGui geometry
// queries inside begin/end refer to its scrolling content child.
ImVec2 folded_window_position();
ImVec2 folded_window_size();
}
