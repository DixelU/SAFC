#include "../imgui/folded_theme.h"
#include "../imgui/mapping_panel.h"
#include "imgui_id_audit.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float epsilon = .05f) { return std::abs(a - b) <= epsilon; }

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    ~context_guard() { ImGui::DestroyContext(context); }
};

struct key_rectangle
{
    ImRect bounds;
    ImU32 color;
};

bool key_color(ImU32 color)
{
    return color == IM_COL32(207, 225, 233, 255) || color == IM_COL32(61, 74, 83, 255);
}

// Read the actual submitted key fills, independent of the panel's private state.
std::vector<key_rectangle> key_rectangles(const ImDrawList& draw)
{
    std::vector<key_rectangle> result;
    for (int i = 0; i + 3 < draw.VtxBuffer.Size; ++i)
    {
        const auto& a = draw.VtxBuffer[i];
        const auto& b = draw.VtxBuffer[i + 1];
        const auto& c = draw.VtxBuffer[i + 2];
        const auto& d = draw.VtxBuffer[i + 3];
        if (!key_color(a.col) || b.col != a.col || c.col != a.col || d.col != a.col) continue;
        if (!near(a.pos.x, d.pos.x) || !near(b.pos.x, c.pos.x)
            || !near(a.pos.y, b.pos.y) || !near(c.pos.y, d.pos.y)) continue;
        const float width = c.pos.x - a.pos.x, height = c.pos.y - a.pos.y;
        if (width <= 0 || width >= 20 || height < 20) continue;
        result.push_back({ImRect(a.pos, c.pos), a.col});
        i += 3;
    }
    return result;
}

std::vector<key_rectangle> row(const std::vector<key_rectangle>& rectangles, float y)
{
    std::vector<key_rectangle> result;
    for (const auto& rectangle : rectangles)
        if (near(rectangle.bounds.Min.y, y)) result.push_back(rectangle);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
        { return a.bounds.Min.x < b.bounds.Min.x; });
    return result;
}

std::optional<float> octave_label_x(const ImDrawList& draw, const char* label, const ImRect& keyboard)
{
    auto* font = ImGui::GetFont();
    const auto glyph_matches = [&](int index, char c)
    {
        if (index + 3 >= draw.VtxBuffer.Size) return false;
        const auto* glyph = font->FindGlyph(static_cast<ImWchar>(c));
        const auto& a = draw.VtxBuffer[index];
        const auto& z = draw.VtxBuffer[index + 2];
        return near(a.uv.x, glyph->U0, .000001f) && near(a.uv.y, glyph->V0, .000001f)
            && near(z.uv.x, glyph->U1, .000001f) && near(z.uv.y, glyph->V1, .000001f);
    };
    for (int i = 0; i < draw.VtxBuffer.Size; ++i)
    {
        const auto position = draw.VtxBuffer[i].pos;
        if (!keyboard.Contains(position)) continue;
        bool matches = true;
        for (int character = 0; label[character]; ++character)
            matches = matches && glyph_matches(i + 4 * character, label[character]);
        if (matches) return position.x;
    }
    return std::nullopt;
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
        io.DisplaySize = {1600, 1300};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_theme();
        mapping_panel panel;
        auto map = std::make_shared<::cut_and_transpose>(0, 255, 0);
        bool open = true;
        auto draw = [&] { panel.draw_key_map("Keyboard preview regression.mid", map, &open); };
        auto frame = [&] { ImGui::NewFrame(); draw(); ImGui::Render(); };
        frame(); frame();
        ImGui::SetWindowPos("Cut & transpose", {10, 10});
        ImGui::SetWindowSize("Cut & transpose", {1100, 1100});
        frame(); frame();
        auto* parent = ImGui::FindWindowByName("Cut & transpose");
        ImGuiWindow* content = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == parent && std::strstr(window->Name, "##folded-content")) content = window;
        require(content != nullptr, "Missing cut-and-transpose content window.");

        auto rectangles = key_rectangles(*content->DrawList);
        require(rectangles.size() == 256, "Standard bank must draw an upper output and lower input keyboard.");
        const float output_y = rectangles.front().bounds.Min.y;
        auto output = row(rectangles, output_y);
        require(output.size() == 128, "Identity output preview did not contain 128 keys.");
        float input_y = output_y;
        for (const auto& rectangle : rectangles) input_y = std::max(input_y, rectangle.bounds.Min.y);
        auto input = row(rectangles, input_y);
        require(input.size() == 128 && input_y > output_y, "Input keyboard must remain below the output preview.");
        const float origin_x = input.front().bounds.Min.x;
        const float cell = input[1].bounds.Min.x - origin_x;
        const ImRect upper({origin_x, output_y}, {origin_x + cell * 128, output.front().bounds.Max.y});
        const ImRect lower({origin_x, input_y}, {origin_x + cell * 128, input.front().bounds.Max.y});
        const auto initial_c4 = octave_label_x(*content->DrawList, "C4", upper);
        const auto lower_c4 = octave_label_x(*content->DrawList, "C4", lower);
        require(initial_c4 && lower_c4 && near(*initial_c4, *lower_c4), "Identity octave labels must align vertically.");

        for (int shift : {12, -12, 1, -1})
        {
            map->transpose_val = static_cast<std::int16_t>(shift); frame();
            const auto c4 = octave_label_x(*content->DrawList, "C4", upper);
            const auto fixed_c4 = octave_label_x(*content->DrawList, "C4", lower);
            // Glyph rendering snaps to pixels; the logical piano spacing is fractional.
            require(c4 && near(*c4 - *initial_c4, -shift * cell, 1.1f),
                "Output C4 label moved in the wrong direction or by the wrong number of semitones.");
            require(fixed_c4 && near(*fixed_c4, *lower_c4), "Transposition moved the input keyboard.");
            const auto current_input = row(key_rectangles(*content->DrawList), input_y);
            require(current_input.size() == 128, "Transposition changed the number of input keys.");
            for (int key = 0; key < 128; ++key)
                require(near(current_input[key].bounds.Min.x, input[key].bounds.Min.x),
                    "Transposition moved an input key's hit position.");
        }

        map->transpose_val = 12; frame();
        const auto shifted_c5 = octave_label_x(*content->DrawList, "C5", upper);
        require(shifted_c5 && near(*shifted_c5, *lower_c4), "At +12, output C5 must align above input C4.");

        // The output preview is passive: selecting source keys continues to use
        // the existing lower keyboard and its bank-scoped native ImGui ID.
        const ImGuiID keyboard_id = ImHashStr("key_keyboard", 0, content->GetID(0));
        require(safc::imgui_test::probe_item_id(keyboard_id, draw) == 1, "Input keyboard ID became missing or ambiguous.");
        const auto mouse_at_key = [&](int local_key, float y)
        {
            io.AddMousePosEvent(origin_x + (local_key + .5f) * cell, y);
            frame();
        };
        mouse_at_key(60, (upper.Min.y + upper.Max.y) * .5f);
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        require(map->min_val == 0 && map->max_val == 255, "Passive output preview edited the source cut range.");
        mouse_at_key(72, (lower.Min.y + lower.Max.y) * .5f);
        require(ImGui::GetHoveredID() == keyboard_id, "Lower keyboard lost its native mouse hit target.");
        io.AddMouseButtonEvent(0, true); frame();
        mouse_at_key(60, (lower.Min.y + lower.Max.y) * .5f);
        io.AddMouseButtonEvent(0, false); frame();
        require(map->min_val == 60 && map->max_val == 72 && map->transpose_val == 12,
            "Dragging the lower keyboard must set input 60..72 while retaining transposition.");
        rectangles = key_rectangles(*content->DrawList);
        for (const float y : {output_y, input_y})
        {
            const auto cut_row = row(rectangles, y);
            require(cut_row.size() == 128, "Cut filtering removed piano geometry instead of dimming it.");
            for (int source = 0; source < 128; ++source)
            {
                const bool retained = source >= 60 && source <= 72;
                const ImU32 expected = retained ? IM_COL32(207, 225, 233, 255) : IM_COL32(61, 74, 83, 255);
                require(cut_row[source].color == expected,
                    "Both pianos must highlight the retained cut range at the same source positions.");
            }
        }

        *map = ::cut_and_transpose(0, 255, 255);
        io.AddMousePosEvent(-100, -100); frame();
        output = row(key_rectangles(*content->DrawList), output_y);
        require(output.size() == 1 && near(output.front().bounds.Min.x, origin_x),
            "+255 preview must show only output 255 above input 0.");
        map->transpose_val = -255; frame();
        require(row(key_rectangles(*content->DrawList), output_y).empty(),
            "-255 preview must not wrap invalid outputs into the standard bank.");

        ImGui::ActivateItemByID(content->GetID("Show extended 128..255 key bank")); frame(); frame();
        rectangles = key_rectangles(*content->DrawList);
        std::vector<float> rows;
        for (const auto& rectangle : rectangles)
            if (std::none_of(rows.begin(), rows.end(), [&](float y) { return near(y, rectangle.bounds.Min.y); }))
                rows.push_back(rectangle.bounds.Min.y);
        std::sort(rows.begin(), rows.end());
        require(rows.size() == 3, "Extended -255 view must contain two input banks and one valid output key.");
        const float extended_output_y = rows[1], extended_input_y = rows[2];
        const auto extended_output = row(rectangles, extended_output_y);
        require(extended_output.size() == 1 && near(extended_output.front().bounds.Min.x, origin_x + 127 * cell),
            "At -255, output 0 must align above extended input 255.");
        const auto extended_input = row(rectangles, extended_input_y);
        require(extended_input.size() == 128, "Extended input bank is incomplete.");
        const ImGuiID extended_id = ImHashStr("key_keyboard", 0, content->GetID(1));
        require(safc::imgui_test::probe_item_id(extended_id, draw) == 1, "Extended keyboard lost its scoped ID.");
        mouse_at_key(12, (extended_input.front().bounds.Min.y + extended_input.front().bounds.Max.y) * .5f);
        require(ImGui::GetHoveredID() == extended_id, "Extended keyboard lost native input targeting.");
        io.AddMouseButtonEvent(0, true); frame();
        mouse_at_key(22, (extended_input.front().bounds.Min.y + extended_input.front().bounds.Max.y) * .5f);
        io.AddMouseButtonEvent(0, false); frame();
        require(map->min_val == 140 && map->max_val == 150, "Extended keyboard drag must preserve the bank offset.");
        require(io.ConfigDebugHighlightIdConflicts, "ID diagnostics were disabled.");
        std::cout << "Key map preview passed: octave alignment, both shift directions, fixed source keys, extreme bounds, extended bank and native cut dragging.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Key map preview regression failed: " << error.what() << '\n';
        return 1;
    }
}
