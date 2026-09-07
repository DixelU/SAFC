#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace safc::imgui_test
{
// Test-only access to the same detector that displays ImGui's ID-conflict
// warning. Seed the previous hover after NewFrame, then submit the real UI.
// ItemHoverable counts matching IDs before testing window/rectangle hover,
// so this also finds duplicates that are elsewhere in the visible panel.
// A clipped item is not submitted and must be tested after scrolling it in.
template<class Draw>
int probe_item_id(ImGuiID id, Draw&& draw)
{
    IM_ASSERT(GImGui && !GImGui->WithinFrameScope);
    ImGui::NewFrame();
    GImGui->HoveredIdPreviousFrame = id;
    GImGui->HoveredIdPreviousFrameItemCount = 0;
    std::forward<Draw>(draw)();
    const int count = GImGui->HoveredIdPreviousFrameItemCount;
    ImGui::Render();
    return count;
}
}
