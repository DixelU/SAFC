#define NOMINMAX
#include <Windows.h>

#include "../imgui/editor_panel.h"
#include "../imgui/playback_session.h"
#include "../imgui/folded_theme.h"
#include "imgui_id_audit.h"
#include "../SAFC_InnerModules/midi_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    ~context_guard() { ImGui::DestroyContext(context); }
};
}

// This is a native ImGui input regression. It uses the production transport
// explicitly configured with its silent output; no physical MIDI or audio sink
// is opened. Geometry is taken from the generated ImGui child window rather
// than screen captures or fixed desktop coordinates.
int main()
{
    try
    {
        const auto directory = std::filesystem::current_path() /
            (L"imgui-editor-regression-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        const auto output = directory / L"editor-native-input.mid";
        safc::imgui_ui::playback_session playback;
        safc::imgui_ui::native_dialogs dialogs;
        dialogs.save_midi = [output](const std::wstring&) { return output.wstring(); };
        safc::imgui_ui::editor_panel editor(playback, std::move(dialogs));
        std::string report;
        const bool smoke_passed = editor.run_smoke(directory.wstring(), report);
        require(smoke_passed, report.c_str());
        std::cout << report << '\n';

        require(playback.open((directory / L"editor-input.mid").wstring(), true, true), "Silent output did not start.");
        const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!playback.snapshot().playing && std::chrono::steady_clock::now() < ready_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(playback.snapshot().playing && playback.snapshot().error.empty(), "Silent playback output did not become ready.");

        context_guard context;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1400, 1000};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int font_width, font_height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
        safc::imgui_ui::apply_theme();
        bool visible = true;
        auto frame = [&]
        {
            ImGui::NewFrame();
            editor.draw(&visible);
            ImGui::Render();
        };
        frame(); frame(); frame();
        require(ImGui::GetDrawData()->TotalVtxCount > 1000, "Editor did not render its native widgets and piano roll.");

        ImGuiWindow* roll = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(window->Name, "Roll panel")) roll = window;
        require(roll != nullptr, "Editor roll child window is absent.");
        const auto origin = roll->DC.CursorStartPos;
        const float size_x = roll->WorkRect.Max.x - origin.x;
        const float size_y = roll->WorkRect.Max.y - origin.y - 34.f;
        auto baseline = std::make_unique<midi_editor>();
        require(baseline->load_file((directory / L"editor-output.mid").wstring()), "Editor baseline could not be reloaded.");
        const auto initial_count = baseline->get_note_count();
        const int low = baseline->get_view_key_low(), high = baseline->get_view_key_high();
        const float key_height = (size_y - 24.f - 110.f - 6.f) / (high - low + 1);
        const auto duration = baseline->get_view_duration_ticks();
        const float note_width = size_x - 47.f;
        auto position = [&](double tick, int key)
        {
            return ImVec2(origin.x + 47.f + float(tick / double(duration)) * note_width,
                origin.y + 24.f + (high - key + .5f) * key_height);
        };
        auto drag = [&](ImVec2 from, ImVec2 to)
        {
            io.AddMousePosEvent(from.x, from.y); frame();
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMousePosEvent(to.x, to.y); frame();
            io.AddMouseButtonEvent(0, false); frame(); frame();
        };
        auto key = [&](ImGuiKey value, bool ctrl)
        {
            if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(value, true); frame();
            io.AddKeyEvent(value, false);
            if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };
        auto save = [&]
        {
            key(ImGuiKey_S, true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.busy() && std::chrono::steady_clock::now() < deadline)
            { std::this_thread::sleep_for(std::chrono::milliseconds(2)); frame(); }
            frame();
            require(!editor.busy() && !editor.has_unsaved_changes(), "Native Ctrl+S did not finish saving the document.");
            auto model = std::make_unique<midi_editor>();
            require(model->load_file(output.wstring()), "Native Ctrl+S did not save a readable MIDI.");
            return model;
        };

        const auto draw_at = position(double(duration) * .5, 76);
        drag(draw_at, {draw_at.x + 35.f, draw_at.y});
        auto saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native canvas draw did not insert one note.");
        midi_editor::piano_note drawn;
        require(saved->find_note_at(duration / 2, 76, drawn), "Native canvas draw used the wrong time or pitch.");
        saved.reset(); // Release the mapped output before the next atomic save.

        auto move_from = position(double(drawn.start_tick) + double(drawn.length()) * .25, 76);
        drag(move_from, {move_from.x + 30.f, move_from.y - key_height * 2.f});
        saved = save();
        const auto after_move = saved->get_all_notes();
        const auto moved = std::find_if(after_move.begin(), after_move.end(), [](const auto& value) { return value.key == 78; });
        require(moved != after_move.end() && moved->start_tick > drawn.start_tick, "Native canvas drag did not move time and pitch.");
        saved.reset();
        key(ImGuiKey_Z, true);
        saved = save();
        midi_editor::piano_note restored;
        require(saved->find_note_at(drawn.start_tick, drawn.key, restored) && restored.length() == drawn.length(), "Native Ctrl+Z did not restore the moved note.");
        saved.reset();
        key(ImGuiKey_Y, true);
        saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native Ctrl+Y changed note count.");
        saved.reset();
        key(ImGuiKey_Delete, false);
        saved = save();
        require(saved->get_note_count() == initial_count, "Native Delete did not erase the selected note.");
        saved.reset();
        key(ImGuiKey_Z, true);
        saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native delete undo did not restore the note.");

        // Track metadata is user data, including literal ImGui label delimiters.
        // Renaming a track must not alter its selectable ID or alias another row.
        const auto named_fixture = directory / L"editor-named-tracks.mid";
        const auto renamed_fixture = directory / L"editor-renamed-tracks.mid";
        saved->set_track_name(0, "A##B###same");
        saved->set_track_name(1, "C##D###same");
        require(saved->save_file(named_fixture.wstring()), "Could not write track-label fixture.");
        saved->set_track_name(0, "Renamed##A###other");
        saved->set_track_name(1, "Renamed##B###other");
        require(saved->save_file(renamed_fixture.wstring()), "Could not write renamed track-label fixture.");
        saved.reset();
        auto load_fixture = [&](const std::filesystem::path& path)
        {
            editor.open_file(path.wstring());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.busy() && std::chrono::steady_clock::now() < deadline)
            { std::this_thread::sleep_for(std::chrono::milliseconds(2)); frame(); }
            frame(); frame();
            require(!editor.busy(), "Track-label fixture load timed out.");
        };
        auto hover_track = [&](int track_id)
        {
            ImGuiWindow* list = nullptr;
            for (auto* window : ImGui::GetCurrentContext()->Windows)
                if (std::strstr(window->Name, "Track list")) list = window;
            require(list != nullptr, "Editor track list child window is absent.");
            const auto at = list->DC.CursorStartPos;
            io.AddMousePosEvent(at.x + 20.f,
                at.y + ImGui::GetTextLineHeightWithSpacing() * track_id + ImGui::GetTextLineHeight() * .5f);
            frame(); frame();
            const auto row_seed = ImHashData(&track_id, sizeof(track_id), list->ID);
            const auto expected = ImHashStr("##track", 0, row_seed);
            require(ImGui::GetHoveredID() == expected, "Track name leaked into its selectable ID.");
            require(ImGui::GetCurrentContext()->HoveredIdPreviousFrameItemCount == 1,
                "Track rows submitted conflicting ImGui IDs.");
            return expected;
        };
        load_fixture(named_fixture);
        const auto first_track = hover_track(0), second_track = hover_track(1);
        require(first_track != second_track, "Track names with matching ### suffixes aliased each other.");
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        require(ImGui::GetCurrentContext()->NavId == second_track, "Native track-row selection targeted the wrong item.");
        load_fixture(renamed_fixture);
        require(hover_track(0) == first_track && hover_track(1) == second_track,
            "Track-row IDs changed when MIDI track names changed.");

        auto content_window = [](const char* title)
        {
            const auto* parent = ImGui::FindWindowByName(title);
            for (auto* window : ImGui::GetCurrentContext()->Windows)
                if (window->ParentWindow == parent && std::strstr(window->Name, "##folded-content")) return window;
            throw std::runtime_error(std::string("Missing folded content: ") + title);
        };
        auto probe = [&](ImGuiID id, const char* label)
        {
            require(safc::imgui_test::probe_item_id(id, [&] { editor.draw(&visible); }) == 1,
                (std::string("Editor widget ID was missing or conflicted: ") + label).c_str());
        };
        auto audit_combo = [&](ImGuiWindow* content, const char* label, const std::vector<const char*>& options, int choice)
        {
            probe(content->GetID(label), label);
            ImGui::ActivateItemByID(content->GetID(label));
            frame(); frame();
            require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Native editor combo did not open.");
            auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
            require(popup != nullptr, "Native editor combo has no popup window.");
            ImGuiID selected = 0;
            for (int i = 0; i < int(options.size()); ++i)
            {
                const auto id = ImHashStr(options[i], 0, popup->GetID(i));
                probe(id, options[i]);
                if (i == choice) selected = id;
            }
            ImGui::ActivateItemByID(selected);
            frame(); frame();
            require(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Native combo selection did not close its popup.");
        };
        struct tool_case { ImGuiKey shortcut; const char* title; std::vector<const char*> controls; };
        const std::vector<tool_case> tools = {
            {ImGuiKey_U, "Chopper", {"Slices / beat", "Time multiplier", "Gap (%)", "Align pattern to score grid"}},
            {ImGuiKey_Y, "Flip score", {"Horizontal", "Preserve start-time pattern", "Vertical"}},
            {ImGuiKey_W, "Claw machine", {"Period (beats)", "Remove every", "Time distortion", "Remove short notes", "Stretch to original length"}},
            {ImGuiKey_O, "LFO", {"Center", "Range", "Cycles", "Phase (degrees)", "Shape"}}
        };
        for (const auto& tool : tools)
        {
            hover_track(0);
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMouseButtonEvent(0, false); frame();
            io.AddKeyEvent(ImGuiMod_Alt, true);
            key(tool.shortcut, false);
            io.AddKeyEvent(ImGuiMod_Alt, false); frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.busy() && std::chrono::steady_clock::now() < deadline)
            { std::this_thread::sleep_for(std::chrono::milliseconds(2)); frame(); }
            frame();
            require(!editor.busy(), "Editor tool preview timed out.");
            ImGui::SetWindowSize(tool.title, ImVec2(560, 650));
            frame(); frame();
            auto* content = content_window(tool.title);
            for (const char* label : tool.controls) probe(content->GetID(label), label);
            probe(content->GetID("Accept"), "Accept");
            probe(content->GetID("Cancel"), "Cancel");
            if (tool.shortcut == ImGuiKey_O)
                audit_combo(content, "Shape", {"Sine", "Triangle", "Square"}, 0);
            ImGui::ActivateItemByID(content->GetID("Cancel"));
            frame(); frame();
        }
        auto* editor_content = content_window("MIDI editor");
        audit_combo(editor_content, "Tool", {"Draw / move", "Select", "Erase"}, 0);
        audit_combo(editor_content, "Snap", {"1/4", "1/8", "1/16", "1/32", "1/64", "Off"}, 2);
        audit_combo(editor_content, "Lane", {"Velocity", "Pitch bend", "Pan", "Volume", "Tempo"}, 4);
        for (const char* label : {"BPM min", "BPM max", "20-400 BPM", "At tick", "BPM", "Insert tempo"})
            probe(editor_content->GetID(label), label);

        editor.shutdown();
        playback.shutdown();
        std::cout << "Native ImGui draw, move, keyboard undo/redo/delete, atomic Save, stable track IDs, four tool ID audits and silent transport passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor regression failed: " << error.what() << '\n';
        return 1;
    }
}
