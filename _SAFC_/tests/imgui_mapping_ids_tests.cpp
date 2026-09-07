#include "../imgui/folded_theme.h"
#include "../imgui/mapping_panel.h"
#include "imgui_id_audit.h"

#include <imgui_internal.h>
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

ImGuiWindow* child_window(ImGuiWindow* parent, const char* fragment)
{
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (window->ParentWindow == parent && std::strstr(window->Name, fragment)) return window;
    throw std::runtime_error(std::string("Missing mapping child: ") + fragment);
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
        io.DisplaySize = {2900, 1500};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        apply_theme();

        mapping_panel panel;
        auto keys = std::make_shared<::cut_and_transpose>(0, 127, 0);
        auto volume = std::make_shared<mapping_panel::volume_curve>();
        volume->insert(0, 0); volume->insert(64, 80); volume->insert(127, 127);
        auto pitch = std::make_shared<mapping_panel::pitch_curve>();
        pitch->insert(0, 0); pitch->insert(8192, 8192); pitch->insert(16383, 16383);
        bool key_open = true, volume_open = true, pitch_open = true;
        auto draw = [&]
        {
            panel.draw_key_map("same##file###name.mid", keys, &key_open);
            panel.draw_volume_map("same##file###name.mid", volume, &volume_open);
            panel.draw_pitch_map("same##file###name.mid", pitch, &pitch_open);
        };
        auto frame = [&] { ImGui::NewFrame(); draw(); ImGui::Render(); };
        frame(); frame();
        const char* titles[] = {"Cut & transpose", "Volume mapping", "Pitch bend mapping"};
        for (int i = 0; i < 3; ++i)
        {
            ImGui::SetWindowPos(titles[i], ImVec2(10.f + i * 950.f, 10));
            ImGui::SetWindowSize(titles[i], ImVec2(930, 1300));
        }
        frame(); frame();
        auto* key_content = child_window(ImGui::FindWindowByName(titles[0]), "##folded-content");
        auto* volume_content = child_window(ImGui::FindWindowByName(titles[1]), "##folded-content");
        auto* pitch_content = child_window(ImGui::FindWindowByName(titles[2]), "##folded-content");
        auto probe = [&](ImGuiID id, const char* label)
        {
            require(safc::imgui_test::probe_item_id(id, draw) == 1,
                std::string("Mapping item was missing or had conflicting IDs: ") + label);
        };
        auto activate = [&](ImGuiID id) { ImGui::ActivateItemByID(id); frame(); frame(); };

        for (const char* label : {"Enable key map", "Reset (0..255)", "Keep 0..127", "0..127 -> 128..255",
            "Copy", "Paste", "First input key", "Last input key", "Transpose (semitones)", "Show extended 128..255 key bank"})
            probe(key_content->GetID(label), label);
        activate(key_content->GetID("Show extended 128..255 key bank"));
        for (int bank = 0; bank < 2; ++bank)
            probe(ImHashStr("key_keyboard", 0, key_content->GetID(bank)), "key keyboard bank");

        for (auto* content : {volume_content, pitch_content})
        {
            for (const char* label : {"Enable mapping", "Identity", "Clear", "Copy", "Paste", "Simplify", "Trace",
                "Power", "Generate power curve", "Single point", "Two-point segment", "curve_canvas",
                "Input", "Output", "Set point", "Delete point", "Control points"})
                probe(content->GetID(label), label);
            for (const char* label : {"Power", "Input", "Output"})
                for (const char* step : {"-", "+"})
                    probe(ImHashStr(step, 0, content->GetID(label)), "numeric step button");
            const char* option = content == volume_content ? "0..255" : "Keep center";
            probe(content->GetID(option), option);
            activate(content->GetID(option));
            probe(content->GetID(option), option);
            activate(content->GetID("Copy"));
            probe(content->GetID("Paste"), "enabled Paste");
            activate(content->GetID("Control points"));
            auto* points = child_window(content, "point_list");
            if (content == volume_content)
                for (const char* label : {"0 -> 0", "64 -> 80", "127 -> 127"}) probe(points->GetID(label), label);
            else
                for (const char* label : {"0 -> 0", "8192 -> 8192", "16383 -> 16383"}) probe(points->GetID(label), label);

            activate(content->GetID("Two-point segment"));
            const auto canvas_id = content->GetID("curve_canvas");
            bool found_canvas = false;
            // Discover a point inside the real canvas hit box, independent of
            // font metrics and toolbar wrapping, before exercising segment mode.
            const float x = (content->WorkRect.Min.x + content->WorkRect.Max.x) * .5f;
            for (float y = content->WorkRect.Min.y; y < content->WorkRect.Max.y; y += 4.f)
            {
                io.AddMousePosEvent(x, y); frame();
                if (ImGui::GetHoveredID() != canvas_id) continue;
                io.AddMouseButtonEvent(0, true); frame();
                io.AddMouseButtonEvent(0, false); frame();
                found_canvas = true;
                break;
            }
            require(found_canvas, "Could not reach the mapping canvas through native mouse input.");
            probe(content->GetID("Cancel segment"), "conditional Cancel segment");
            activate(content->GetID("Cancel segment"));
            activate(content->GetID("Single point"));
            activate(content->GetID("Enable mapping"));
            probe(content->GetID("Enable mapping"), "disabled mapping branch");
            require(safc::imgui_test::probe_item_id(canvas_id, draw) == 0,
                "Disabled mapping still submitted its canvas.");
            activate(content->GetID("Enable mapping"));
            probe(canvas_id, "reenabled mapping canvas");
        }
        activate(key_content->GetID("Enable key map"));
        probe(key_content->GetID("Enable key map"), "disabled key map branch");
        require(!keys, "Native key map toggle did not disable mapping.");
        activate(key_content->GetID("Enable key map"));
        require(bool(keys), "Native key map toggle did not reenable mapping.");
        require(io.ConfigDebugHighlightIdConflicts, "ID conflict diagnostics were disabled.");
        std::cout << "Mapping IDs passed: three windows, extended keyboards, numeric steps, expanded points, segment mode and disabled branches.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Mapping ID regression failed: " << error.what() << '\n';
        return 1;
    }
}
