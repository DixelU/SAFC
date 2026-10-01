#include "folded_theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace safc::imgui_ui
{
namespace
{
// Only matching Begin/End bookkeeping; application state belongs to the caller.
struct folded_frame
{
	bool has_content_child = false;
	ImVec2 position;
	ImVec2 size;
};
thread_local std::vector<folded_frame> folded_frames;

enum class window_placement
{
	normal,
	maximized,
	// Leaves the maximized area on the next Begin.
	restoring
};

// Placement lives in each window's own ImGui storage, so it follows the context.
const ImGuiID placement_key = ImHashStr("##folded-placement");
const ImGuiID restore_position_key = ImHashStr("##folded-restore-position");
const ImGuiID restore_size_key = ImHashStr("##folded-restore-size");
// An empty size selects the main viewport.
thread_local ImVec2 maximized_position, maximized_size;

window_placement placement_of(ImGuiWindow* window)
{
	return window ? static_cast<window_placement>(window->StateStorage.GetInt(placement_key))
				  : window_placement::normal;
}

void set_placement(ImGuiWindow& window, window_placement placement)
{
	window.StateStorage.SetInt(placement_key, static_cast<int>(placement));
}

ImVec2 stored_vector(ImGuiWindow& window, ImGuiID key)
{
	return {window.StateStorage.GetFloat(key), window.StateStorage.GetFloat(key + 1)};
}

void store_vector(ImGuiWindow& window, ImGuiID key, ImVec2 value)
{
	window.StateStorage.SetFloat(key, value.x);
	window.StateStorage.SetFloat(key + 1, value.y);
}

void maximize(ImGuiWindow& window)
{
	store_vector(window, restore_position_key, window.Pos);
	store_vector(window, restore_size_key, window.SizeFull);
	// A maximized window is not saved, so its layout entry must hold this placement.
	if (auto* settings = ImGui::FindWindowSettingsByWindow(&window))
	{
		settings->Pos = ImVec2ih(window.Pos);
		settings->Size = ImVec2ih(window.SizeFull);
	}
	set_placement(window, window_placement::maximized);
}

void draw_maximize_icon(ImDrawList& draw, ImVec2 center, float scale, ImU32 color, bool maximized)
{
	const float thickness = 1.5f * scale;
	if (!maximized)
	{
		const float half = 5.f * scale;
		draw.AddRect({center.x - half, center.y - half}, {center.x + half, center.y + half}, color, 0.f,
			ImDrawFlags_None, thickness);
		draw.AddLine({center.x - half, center.y - half + thickness}, {center.x + half, center.y - half + thickness},
			color, thickness);
		return;
	}
	// Two stacked outlines: the front one, and the visible corner of the one behind.
	const float outer = 5.f * scale;
	const float inner = 2.f * scale;
	draw.AddRect({center.x - outer, center.y - inner}, {center.x + inner, center.y + outer}, color, 0.f,
		ImDrawFlags_None, thickness);
	const ImVec2 behind[] = {{center.x - inner, center.y - inner}, {center.x - inner, center.y - outer},
		{center.x + outer, center.y - outer}, {center.x + outer, center.y + inner}, {center.x + inner, center.y + inner}};
	draw.AddPolyline(behind, IM_ARRAYSIZE(behind), color, ImDrawFlags_None, thickness);
}

void draw_chrome(ImDrawList& draw, ImVec2 pos, ImVec2 size, float caption, float scale, bool focused, bool resizeable)
{
	const float x = pos.x + 0.5f;
	const float y = pos.y + 0.5f;
	const float right = pos.x + size.x - 0.5f;
	const float bottom = pos.y + size.y - 0.5f;
	const float hat = 3.f * scale;
	const float notch = 5.f * scale;
	const float header_bottom = y + caption;
	const float hat_left = x + size.x * 0.24f;
	const float hat_right = right - size.x * 0.24f;
	const float body_height = std::max(0.f, bottom - header_bottom);
	const float top_fold = header_bottom + body_height * 0.29f;
	const float bottom_fold = bottom - body_height * 0.22f;
	const ImU32 body = ImGui::GetColorU32(ImVec4(0.035f, 0.066f, 0.10f, 0.97f));
	const ImU32 header =
		ImGui::GetColorU32(focused ? ImVec4(0.17f, 0.32f, 0.46f, 1.f) : ImVec4(0.13f, 0.23f, 0.32f, 1.f));
	const ImVec2 title_shape[] = {{x, header_bottom}, {x, y + hat}, {hat_left, y + hat}, {hat_left + hat, y},
		{hat_right - hat, y}, {hat_right, y + hat}, {right, y + hat}, {right, header_bottom}};
	// The stepped header is concave: use a rectangle and a convex hat separately.
	draw.AddRectFilled({x, y + hat}, {right, header_bottom}, header);
	const ImVec2 hat_shape[] = {{hat_left, y + hat}, {hat_left + hat, y}, {hat_right - hat, y}, {hat_right, y + hat}};
	draw.AddConvexPolyFilled(hat_shape, IM_ARRAYSIZE(hat_shape), header);

	// Three convex panels reproduce the inset waist without concave tessellation.
	const ImVec2 top_panel[] = {{x, header_bottom}, {right, header_bottom}, {right, top_fold},
		{right - notch, top_fold + notch}, {x + notch, top_fold + notch}, {x, top_fold}};
	const ImVec2 bottom_panel[] = {{x + notch, bottom_fold - notch}, {right - notch, bottom_fold - notch},
		{right, bottom_fold}, {right, bottom}, {x, bottom}, {x, bottom_fold}};
	draw.AddConvexPolyFilled(top_panel, IM_ARRAYSIZE(top_panel), body);
	draw.AddRectFilled({x + notch, top_fold + notch}, {right - notch, bottom_fold - notch}, body);
	draw.AddConvexPolyFilled(bottom_panel, IM_ARRAYSIZE(bottom_panel), body);

	const ImVec2 top_outline[] = {{x + notch, top_fold + notch}, {x, top_fold}, {x, header_bottom},
		{right, header_bottom}, {right, top_fold}, {right - notch, top_fold + notch}};
	const ImVec2 bottom_outline[] = {{x + notch, bottom_fold - notch}, {x, bottom_fold}, {x, bottom}, {right, bottom},
		{right, bottom_fold}, {right - notch, bottom_fold - notch}};
	// Deliberately leave the waist edges open. A soft second pass matches SAFGUIF.
	for (int pass = 0; pass < 2; ++pass)
	{
		const ImU32 border = ImGui::GetColorU32(ImVec4(0.64f, 0.76f, 0.83f, (focused ? 0.76f : 0.50f) / (pass + 1.f)));
		const float thickness = (pass + 1.f) * scale;
		draw.AddPolyline(title_shape, IM_ARRAYSIZE(title_shape), border, ImDrawFlags_None, thickness);
		draw.AddPolyline(top_outline, IM_ARRAYSIZE(top_outline), border, ImDrawFlags_None, thickness);
		draw.AddPolyline(bottom_outline, IM_ARRAYSIZE(bottom_outline), border, ImDrawFlags_None, thickness);
	}
	if (resizeable)
	{
		const ImU32 grip = ImGui::GetColorU32(ImGuiCol_Separator);
		draw.AddLine(
			{right - 12.f * scale, bottom - 3.f * scale}, {right - 3.f * scale, bottom - 12.f * scale}, grip, scale);
		draw.AddLine(
			{right - 7.f * scale, bottom - 3.f * scale}, {right - 3.f * scale, bottom - 7.f * scale}, grip, scale);
	}
}
}

void apply_theme(float scale)
{
	scale = std::isfinite(scale) ? std::clamp(scale, 0.75f, 3.f) : 1.f;
	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle{};
	ImGui::StyleColorsDark(&style);
	style.WindowPadding = {16.f, 12.f};
	style.WindowMinSize = {180.f, 120.f};
	style.WindowRounding = 0.f;
	style.WindowBorderSize = 0.f;
	style.ChildRounding = 0.f;
	style.FrameRounding = 0.f;
	style.GrabRounding = 0.f;
	style.PopupRounding = 0.f;
	style.TabRounding = 0.f;
	style.FrameBorderSize = 1.f;
	style.FramePadding = {8.f, 5.f};
	style.ItemSpacing = {10.f, 8.f};
	style.CellPadding = {6.f, 5.f};
	style.WindowTitleAlign = {0.5f, 0.5f};
	auto* colors = style.Colors;
	colors[ImGuiCol_Text] = {0.90f, 0.94f, 0.97f, 1.f};
	colors[ImGuiCol_TextDisabled] = {0.47f, 0.59f, 0.67f, 1.f};
	colors[ImGuiCol_WindowBg] = {0.035f, 0.066f, 0.10f, 1.f};
	colors[ImGuiCol_ChildBg] = {0.045f, 0.09f, 0.14f, 0.92f};
	colors[ImGuiCol_PopupBg] = {0.04f, 0.08f, 0.13f, 0.99f};
	colors[ImGuiCol_Border] = {0.12f, 0.39f, 0.59f, 0.80f};
	colors[ImGuiCol_FrameBg] = {0.04f, 0.11f, 0.18f, 1.f};
	colors[ImGuiCol_FrameBgHovered] = {0.08f, 0.23f, 0.35f, 1.f};
	colors[ImGuiCol_FrameBgActive] = {0.08f, 0.30f, 0.46f, 1.f};
	colors[ImGuiCol_Button] = {0.04f, 0.19f, 0.31f, 1.f};
	colors[ImGuiCol_ButtonHovered] = {0.07f, 0.34f, 0.52f, 1.f};
	colors[ImGuiCol_ButtonActive] = {0.06f, 0.43f, 0.65f, 1.f};
	colors[ImGuiCol_Header] = {0.09f, 0.26f, 0.39f, 1.f};
	colors[ImGuiCol_HeaderHovered] = {0.11f, 0.36f, 0.52f, 1.f};
	colors[ImGuiCol_HeaderActive] = {0.11f, 0.43f, 0.61f, 1.f};
	colors[ImGuiCol_CheckMark] = {0.20f, 0.88f, 0.81f, 1.f};
	colors[ImGuiCol_SliderGrab] = {0.20f, 0.66f, 0.87f, 1.f};
	colors[ImGuiCol_SliderGrabActive] = {0.38f, 0.86f, 1.f, 1.f};
	colors[ImGuiCol_Separator] = {0.20f, 0.47f, 0.63f, 0.85f};
	colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_SliderGrab];
	colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_SliderGrabActive];
	colors[ImGuiCol_Tab] = colors[ImGuiCol_FrameBg];
	colors[ImGuiCol_TabHovered] = colors[ImGuiCol_HeaderHovered];
	colors[ImGuiCol_TabSelected] = colors[ImGuiCol_Header];
	colors[ImGuiCol_TextSelectedBg] = {0.14f, 0.56f, 0.79f, 0.45f};
	colors[ImGuiCol_NavCursor] = {0.35f, 0.86f, 1.f, 1.f};
	// Fonts are built at their final pixel size by the caller.
	style.ScaleAllSizes(scale);
}

bool begin_folded_window(const char* title, bool* open, ImGuiWindowFlags flags)
{
	const bool can_maximize = (flags & ImGuiWindowFlags_NoResize) == 0;
	ImGuiWindow* existing = can_maximize ? ImGui::FindWindowByName(title) : nullptr;
	const window_placement placement = placement_of(existing);
	if (placement == window_placement::restoring)
	{
		ImGui::SetNextWindowPos(stored_vector(*existing, restore_position_key), ImGuiCond_Always);
		ImGui::SetNextWindowSize(stored_vector(*existing, restore_size_key), ImGuiCond_Always);
		set_placement(*existing, window_placement::normal);
	}
	const bool maximized = placement == window_placement::maximized;
	if (maximized)
	{
		const auto* viewport = ImGui::GetMainViewport();
		const bool custom_area = maximized_size.x > 0.f && maximized_size.y > 0.f;
		ImGui::SetNextWindowPos(custom_area ? maximized_position : viewport->WorkPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(custom_area ? maximized_size : viewport->WorkSize, ImGuiCond_Always);
		// The saved layout keeps the placement to restore.
		flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
	}

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	const bool visible = ImGui::Begin(title, nullptr,
		flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	folded_frames.push_back({false, ImGui::GetWindowPos(), ImGui::GetWindowSize()});
	if (!visible)
		return false;

	const ImVec2 pos = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();
	// The folded chrome was authored against ImGui's 13-pixel default font.
	constexpr float caption_reference_font_size = 13.f;
	const float scale = ImGui::GetFontSize() / caption_reference_font_size;
	const float caption = ImGui::GetFrameHeight() + 6.f * scale;
	const ImVec2 maximum = {pos.x + size.x, pos.y + size.y};
	ImGui::PushClipRect(pos, maximum, false);
	auto& draw = *ImGui::GetWindowDrawList();
	draw_chrome(draw, pos, size, caption, scale, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows),
		(flags & ImGuiWindowFlags_NoResize) == 0);

	// Caption buttons fill caption-wide slots counted from the right edge.
	const int maximize_slot = open ? 1 : 0;
	const float buttons_width = caption * float(maximize_slot + (can_maximize ? 1 : 0));
	ImGui::SetCursorScreenPos({pos.x + 2.f * scale, pos.y + 4.f * scale});
	ImGui::InvisibleButton(
		"##folded-caption", {std::max(1.f, size.x - buttons_width - 4.f * scale), caption - 4.f * scale});
	const bool dragging =
		!(flags & ImGuiWindowFlags_NoMove) && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
	bool toggle_maximized =
		can_maximize && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	const char* label_end = std::strstr(title, "##");
	if (!label_end)
		label_end = title + std::strlen(title);
	const ImVec2 text_size = ImGui::CalcTextSize(title, label_end);
	const ImVec4 title_clip = {pos.x + 10.f * scale, pos.y, maximum.x - buttons_width - 6.f * scale, pos.y + caption};
	draw.AddText(nullptr, 0.f,
		{std::max(title_clip.x, pos.x + (size.x - text_size.x) * 0.5f), pos.y + (caption - text_size.y) * 0.5f + scale},
		ImGui::GetColorU32(ImGuiCol_Text), title, label_end, 0.f, &title_clip);

	struct caption_button_state
	{
		bool pressed;
		ImVec2 center;
		ImU32 color;
	};
	const auto caption_button = [&](const char* id, int slot, const char* tooltip) -> caption_button_state
	{
		const float button_size = caption - 8.f * scale;
		const ImVec2 corner = {maximum.x - caption * float(slot + 1) + 2.f * scale, pos.y + 5.f * scale};
		ImGui::SetCursorScreenPos(corner);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		const bool pressed = ImGui::Button(id, {button_size, button_size});
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
			ImGui::SetTooltip("%s", tooltip);
		return {pressed, {corner.x + button_size * 0.5f, corner.y + button_size * 0.5f},
			ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_Separator)};
	};
	if (open)
	{
		const auto close = caption_button("##folded-close", 0, "Close");
		if (close.pressed)
			*open = false;
		const float radius = 6.f * scale;
		draw.AddLine(close.center, {close.center.x, close.center.y + radius}, close.color, 2.f * scale);
		draw.AddLine(close.center, {close.center.x - radius, close.center.y - radius * 0.55f}, close.color,
			2.f * scale);
		draw.AddLine(close.center, {close.center.x + radius, close.center.y - radius * 0.55f}, close.color,
			2.f * scale);
	}
	if (can_maximize)
	{
		const auto button = caption_button("##folded-maximize", maximize_slot, maximized ? "Restore" : "Maximize");
		toggle_maximized |= button.pressed;
		draw_maximize_icon(draw, button.center, scale, button.color, maximized);
	}
	ImGui::PopClipRect();
	// Takes effect on the next Begin, which places the window before its content.
	if (toggle_maximized && maximized)
		set_placement(*ImGui::GetCurrentWindow(), window_placement::restoring);
	else if (toggle_maximized)
		maximize(*ImGui::GetCurrentWindow());

	// Apply at the end of caption submission so its hit target stays coherent.
	if (dragging)
	{
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		ImGui::SetWindowPos({pos.x + delta.x, pos.y + delta.y});
	}
	const ImVec2 padding = ImGui::GetStyle().WindowPadding;
	const ImVec2 content_pos = ImGui::GetWindowPos();
	folded_frames.back().position = content_pos;
	ImGui::SetCursorScreenPos({content_pos.x + padding.x, content_pos.y + caption + padding.y});
	// A native child reserves the caption and gives content its own clipping and
	// scrollbars, submitted after the chrome instead of covered by its fill.
	constexpr ImGuiWindowFlags scroll_flags = ImGuiWindowFlags_HorizontalScrollbar |
		ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
	const bool content_visible = ImGui::BeginChild("##folded-content",
		{std::max(1.f, size.x - 2.f * padding.x), std::max(1.f, size.y - caption - 2.f * padding.y)},
		ImGuiChildFlags_None, flags & scroll_flags);
	ImGui::PopStyleColor();
	folded_frames.back().has_content_child = true;
	return content_visible && (!open || *open);
}

void end_folded_window()
{
	IM_ASSERT(!folded_frames.empty());
	if (folded_frames.back().has_content_child)
		ImGui::EndChild();
	folded_frames.pop_back();
	ImGui::End();
}

void set_maximized_window_area(ImVec2 position, ImVec2 size)
{
	maximized_position = position;
	maximized_size = size;
}

void restore_maximized_windows()
{
	for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
	{
		if (placement_of(window) == window_placement::normal)
			continue;
		ImGui::SetWindowPos(window, stored_vector(*window, restore_position_key), ImGuiCond_Always);
		ImGui::SetWindowSize(window, stored_vector(*window, restore_size_key), ImGuiCond_Always);
		set_placement(*window, window_placement::normal);
	}
}

ImVec2 folded_window_position()
{
	IM_ASSERT(!folded_frames.empty());
	return folded_frames.back().position;
}

ImVec2 folded_window_size()
{
	IM_ASSERT(!folded_frames.empty());
	return folded_frames.back().size;
}
}
