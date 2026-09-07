#include "../imgui/folded_theme.h"
#include "../imgui/mapping_panel.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    ~context_guard() { ImGui::DestroyContext(context); }
};

ImGuiWindow* content_window(const char* title)
{
    auto* parent = ImGui::FindWindowByName(title);
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (window->ParentWindow == parent && std::strstr(window->Name, "##folded-content")) return window;
    throw std::runtime_error("Missing curve content window.");
}

ImRect canvas_bounds(ImGuiWindow* content)
{
    // Read the actual filled canvas geometry, independently of toolbar spacing.
    // Its opaque background is distinct from the folded window and controls.
    ImRect bounds(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
    int vertices = 0;
    for (const auto& vertex : content->DrawList->VtxBuffer)
    {
        if (vertex.col != IM_COL32(6, 19, 30, 255)) continue;
        bounds.Add(vertex.pos);
        ++vertices;
    }
    require(vertices == 4 && bounds.GetHeight() == 310.f, "Could not identify the rendered curve canvas: "
        + std::to_string(vertices) + " background vertices, height " + std::to_string(bounds.GetHeight()) + ".");
    return bounds;
}

void require_bounds(ImGuiWindow* content, const ImRect& expected, const char* stage)
{
    const auto actual = canvas_bounds(content);
    require(actual.Min.x == expected.Min.x && actual.Min.y == expected.Min.y
        && actual.Max.x == expected.Max.x && actual.Max.y == expected.Max.y,
        std::string("Canvas moved ") + stage + ": expected (" + std::to_string(expected.Min.x)
        + ", " + std::to_string(expected.Min.y) + ")..(" + std::to_string(expected.Max.x)
        + ", " + std::to_string(expected.Max.y) + "), got (" + std::to_string(actual.Min.x)
        + ", " + std::to_string(actual.Min.y) + ")..(" + std::to_string(actual.Max.x)
        + ", " + std::to_string(actual.Max.y) + ")");
}

template<class Curve, class Draw>
void check_segment_layout(const char* title, std::shared_ptr<Curve>& curve, int maximum, Draw&& draw)
{
    auto& io = ImGui::GetIO();
    auto frame = [&] { ImGui::NewFrame(); draw(); ImGui::Render(); };
    frame(); frame();
    ImGui::SetWindowPos(title, {20, 20});
    ImGui::SetWindowSize(title, {900, 900});
    frame(); frame();
    auto* content = content_window(title);
    const auto initial = canvas_bounds(content);

    auto hover_control = [&](const char* label)
    {
        const auto id = content->GetID(label);
        for (float y = content->WorkRect.Min.y; y < initial.Min.y; y += 4.f)
            for (float x = content->WorkRect.Min.x; x < content->WorkRect.Max.x; x += 6.f)
            {
                io.AddMousePosEvent(x, y);
                frame();
                if (ImGui::GetHoveredID() == id) return ImVec2(x, y);
            }
        throw std::runtime_error(std::string("Could not reach curve control through mouse input: ") + label);
    };
    auto click_at = [&](ImVec2 position)
    {
        io.AddMousePosEvent(position.x, position.y); frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame();
        require_bounds(content, initial, "during mouse press");
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame();
        frame();
    };
    auto curve_position = [&](int x, int y)
    {
        return ImVec2(initial.Min.x + initial.GetWidth() * float(x) / maximum,
            initial.Max.y - initial.GetHeight() * float(y) / maximum);
    };
    auto has_point_near = [&](int key, int value)
    {
        // ImGui rounds mouse positions to physical pixels before hit testing.
        const auto x_tolerance = int(std::ceil(maximum / initial.GetWidth()));
        const auto y_tolerance = int(std::ceil(maximum / initial.GetHeight()));
        return std::any_of(curve->points().begin(), curve->points().end(), [&](const auto& point)
        {
            return std::abs(int(point.first) - key) <= x_tolerance
                && std::abs(int(point.second) - value) <= y_tolerance;
        });
    };

    click_at(hover_control("Two-point segment"));
    require_bounds(content, initial, "before starting a segment");
    const int first_x = maximum / 4, first_y = maximum / 3;
    const int last_x = maximum * 3 / 4, last_y = maximum * 2 / 3;
    const auto original = curve->points();
    click_at(curve_position(first_x, first_y));
    require(curve->points() == original, "The first segment click changed the curve prematurely.");
    require_bounds(content, initial, "while a segment is pending");
    const auto cancel_position = hover_control("Cancel segment");
    click_at(curve_position(last_x, last_y));
    require_bounds(content, initial, "after completing a segment");
    std::string endpoints;
    for (const auto& [x, y] : curve->points()) endpoints += " " + std::to_string(x) + ":" + std::to_string(y);
    require(curve->size() == 4 && has_point_near(first_x, first_y) && has_point_near(last_x, last_y),
        "Native segment clicks did not insert both requested endpoints:" + endpoints);

    const auto completed = curve->points();
    click_at(curve_position(maximum / 2, maximum / 4));
    require_bounds(content, initial, "while another segment is pending");
    click_at(cancel_position);
    require_bounds(content, initial, "after cancelling a segment");
    require(curve->points() == completed, "Cancelling a segment changed the curve.");

    // A click after cancellation must start a fresh segment, proving that the
    // toolbar action actually cancelled rather than merely preserving geometry.
    click_at(curve_position(maximum / 3, maximum / 5));
    require(curve->points() == completed, "Cancellation left a stale segment endpoint active.");
    require_bounds(content, initial, "after restarting a cancelled segment");
    click_at(cancel_position);
    require_bounds(content, initial, "after a second cancellation");
    std::cout << title << " canvas stays (" << initial.Min.x << ", " << initial.Min.y << ")..("
        << initial.Max.x << ", " << initial.Max.y << ") through start, completion and cancellation.\n";
}
}

int main()
{
    try
    {
        using namespace safc::imgui_ui;
        context_guard context;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1400, 1200};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_theme();
        mapping_panel panel;
        auto volume = std::make_shared<mapping_panel::volume_curve>();
        volume->insert(0, 0); volume->insert(255, 255);
        auto pitch = std::make_shared<mapping_panel::pitch_curve>();
        pitch->insert(0, 0); pitch->insert(16383, 16383);
        bool open = true;
        check_segment_layout("Volume mapping", volume, 255,
            [&] { panel.draw_volume_map("Segment layout.mid", volume, &open); });
        check_segment_layout("Pitch bend mapping", pitch, 16383,
            [&] { panel.draw_pitch_map("Segment layout.mid", pitch, &open); });
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Curve layout regression failed: " << error.what() << '\n';
        return 1;
    }
}
