#pragma once
#include <imgui.h>
#include <algorithm>
#include <string_view>

namespace safc::imgui_ui
{
// Selectable identity belongs to the model, not the displayed name. In loops,
// scope id with PushID(model_id). Literal user text must not become ##/### syntax.
inline bool literal_selectable(const char* id, std::string_view text, bool selected = false,
    ImGuiSelectableFlags flags = 0, ImVec2 size = {})
{
    const auto position = ImGui::GetCursorScreenPos();
    const auto text_size = text.empty() ? ImVec2{} : ImGui::CalcTextSize(text.data(), text.data() + text.size(), false);
    if (size.x == 0 && !(flags & ImGuiSelectableFlags_SpanAllColumns))
        size.x = (std::max)(text_size.x, ImGui::GetContentRegionAvail().x);
    if (size.y == 0) size.y = (std::max)(text_size.y, ImGui::GetTextLineHeight());
    const bool pressed = ImGui::Selectable(id, selected, flags, size);
    if (!text.empty() && ImGui::IsItemVisible())
        ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text),
            text.data(), text.data() + text.size());
    return pressed;
}

// Keep the stock combo frame, popup, and input handling while displaying a
// literal preview. BeginCombo's own preview parser treats ## as hidden text.
inline bool begin_literal_combo(const char* id, std::string_view preview)
{
    auto* parent_draw = ImGui::GetWindowDrawList();
    const auto position = ImGui::GetCursorScreenPos();
    const auto padding = ImGui::GetStyle().FramePadding;
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    const bool opened = ImGui::BeginCombo(id, "");
    // BeginCombo switches the current window when the popup opens. Draw on the
    // captured parent and keep the preview clear of the native arrow button.
    const ImVec4 clip{position.x + padding.x, position.y + padding.y,
        position.x + width - height, position.y + height - padding.y};
    if (!preview.empty() && clip.z > clip.x && clip.w > clip.y)
        parent_draw->AddText(nullptr, 0.f, {clip.x, clip.y}, color,
            preview.data(), preview.data() + preview.size(), 0.f, &clip);
    return opened;
}
}
